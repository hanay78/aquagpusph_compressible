/*
 *  This file is part of AQUAgpusph, a free CFD program based on SPH.
 *  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
 *
 *  AQUAgpusph is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AQUAgpusph is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with AQUAgpusph.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 * @brief Tools virtual environment to allow the user to define/manipulate the
 * tools used to carry out the simulation.
 * (see Aqua::CalcServer::Tool)
 */

#include <CalcServer/Tool.h>
#include <CalcServer.h>
#include <InputOutput/Logger.h>
#include <sys/time.h>
#include <queue>
#include <algorithm>

namespace Aqua{ namespace CalcServer{

Tool::Tool(const std::string tool_name, bool once)
    : _name(tool_name)
    , _once(once)
    , _next_tool(NULL)
    , _allocated_memory(0)
    , _n_iters(0)
    , _elapsed_time(0.f)
    , _average_elapsed_time(0.f)
    , _squared_elapsed_time(0.f)
{
}

Tool::~Tool()
{
}

void Tool::setup()
{
    std::vector<Tool*> tools = CalcServer::singleton()->tools();

    // Get the next tool in the pipeline
    int i = id_in_pipeline();
    // There are some tools which can be out of the pipeline (e.g. UnSort or
    // reports)
    if((i >= 0) && (i + 1 < tools.size())) {
        next_tool(tools.at(i + 1));
    }
    return;
}

void Tool::execute()
{
    if(_once && (_n_iters > 0))
        return;

    cl_int err_code;
    timeval tic, tac;

    gettimeofday(&tic, NULL);

    // Launch the tool
    std::vector<cl_event> events = getEvents();
    cl_event event = _execute(events);

    if(event != NULL) {
        // Replace the dependencies event by the new one
        std::vector<InputOutput::Variable*> vars = getDependencies();
        for(auto it = vars.begin(); it < vars.end(); it++){
            (*it)->setEvent(event);
        }

        // Release the event now that it is retained by its users
        err_code = clReleaseEvent(event);
        if(err_code != CL_SUCCESS){
            std::stringstream msg;
            msg << "Failure releasing the new event in tool \"" <<
                name() << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL execution error");
        }
    }

    // Release the events in the wait list, which were retained by getEvents()
    for(auto it = events.begin(); it < events.end(); it++){
        err_code = clReleaseEvent((*it));
        if(err_code != CL_SUCCESS){
            std::stringstream msg;
            msg << "Failure releasing a predecessor event in \"" <<
                name() << "\" tool." << std::endl;
            LOG(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL execution error");
        }
    }    

    gettimeofday(&tac, NULL);

    float elapsed_seconds;
    elapsed_seconds = (float)(tac.tv_sec - tic.tv_sec);
    elapsed_seconds += (float)(tac.tv_usec - tic.tv_usec) * 1E-6f;

    addElapsedTime(elapsed_seconds);
}

int Tool::id_in_pipeline()
{
    std::vector<Tool*> tools = CalcServer::singleton()->tools();
    auto it = std::find(tools.begin(), tools.end(), this);
    if(it == tools.end())
        return -1;
    return std::distance(tools.begin(), it);
}

void Tool::addElapsedTime(float elapsed_time)
{
    _elapsed_time = elapsed_time;
    // Invert the average computation
    _average_elapsed_time *= _n_iters;
    _squared_elapsed_time *= _n_iters;
    // Add the new data
    _average_elapsed_time += elapsed_time;
    _squared_elapsed_time += elapsed_time * elapsed_time;
    // And average it again
    _n_iters++;
    _average_elapsed_time /= _n_iters;
    _squared_elapsed_time /= _n_iters;
}

void Tool::setDependencies(std::vector<std::string> var_names)
{
    InputOutput::Variables *vars = CalcServer::singleton()->variables();
    _vars.clear();
    for(auto it = var_names.begin(); it < var_names.end(); it++){
        InputOutput::Variable *var = vars->get(*it);
        if(!var){
            std::stringstream msg;
            msg << "The tool \"" << name()
                << "\" is asking the undeclared variable \""
                << *it << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            throw std::runtime_error("Invalid variable");
        }
        _vars.push_back(var);
    }

}

void Tool::setDependencies(std::vector<InputOutput::Variable*> vars)
{
    _vars = vars;
}

const std::vector<InputOutput::Variable*> Tool::getDependencies()
{
    return _vars;
}

std::vector<cl_kernel> Tool::compile(const std::string source,
                                     const std::vector<std::string> names,
                                     const std::string additional_flags)
{
    cl_int err_code;
    cl_program program;
    std::vector<cl_kernel> kernels;
    Aqua::CalcServer::CalcServer *C = Aqua::CalcServer::CalcServer::singleton();

    if(!names.size()) {
        LOG(L_WARNING, "compile() called without kernel names\n");
        return kernels;
    }

    std::ostringstream flags;
    #ifdef AQUA_DEBUG
        flags << " -DDEBUG ";
    #else
        flags << " -DNDEBUG ";
    #endif
    flags << " -cl-mad-enable -cl-fast-relaxed-math";
    #ifdef HAVE_3D
        flags << " -DHAVE_3D";
    #else
        flags << " -DHAVE_2D";
    #endif
    flags << " " << additional_flags;

    size_t source_length = source.size();
    const char* source_cstr = source.c_str();
    program = clCreateProgramWithSource(C->context(),
                                        1,
                                        &source_cstr,
                                        &source_length,
                                        &err_code);
    if(err_code != CL_SUCCESS) {
        LOG(L_ERROR, "Failure creating the OpenCL program\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
    err_code = clBuildProgram(program, 0, NULL, flags.str().c_str(), NULL, NULL);
    if(err_code != CL_SUCCESS) {
        LOG(L_ERROR, "Error compiling the OpenCL script\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        LOG0(L_ERROR, "--- Build log ---------------------------------\n");
        size_t log_size = 0;
        clGetProgramBuildInfo(program,
                              C->device(),
                              CL_PROGRAM_BUILD_LOG,
                              0,
                              NULL,
                              &log_size);
        char *log = (char*)malloc(log_size + sizeof(char));
        if(!log){
            std::stringstream msg;
            msg << "Failure allocating " << log_size
                << " bytes for the building log" << std::endl;
            LOG0(L_ERROR, msg.str());
            LOG0(L_ERROR, "--------------------------------- Build log ---\n");
            throw std::bad_alloc();
        }
        strcpy(log, "");
        clGetProgramBuildInfo(program,
                              C->device(),
                              CL_PROGRAM_BUILD_LOG,
                              log_size,
                              log,
                              NULL);
        strcat(log, "\n");
        LOG0(L_DEBUG, log);
        LOG0(L_ERROR, "--------------------------------- Build log ---\n");
        free(log); log=NULL;
        clReleaseProgram(program);
        throw std::runtime_error("OpenCL compilation error");
    }
    for(auto name : names) {
        kernels.push_back(clCreateKernel(program, name.c_str(), &err_code));
        if(err_code != CL_SUCCESS) {
            std::stringstream msg;
            msg << "Failure creating the \"" << name << "\" OpenCL kernel"
                << std::endl;
            LOG0(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            LOG0(L_ERROR, "--- Source ---------------------------------\n");
            LOG0(L_ERROR, source);
            LOG0(L_ERROR, "--------------------------------- Source ---\n");
            throw std::runtime_error("OpenCL error");
        }
    }

    return kernels;
}

cl_kernel Tool::compile_kernel(const std::string source,
                               const std::string kernel_name,
                               const std::string additional_flags)
{
    return compile(source, {kernel_name}, additional_flags).at(0);
}

const std::vector<cl_event> Tool::getEvents()
{
    cl_int err_code;
    _events.clear();
    for(auto it = _vars.begin(); it < _vars.end(); it++){
        cl_event event = (*it)->getEvent();
        if(std::find(_events.begin(), _events.end(), event) != _events.end())
            continue;
        // Retain the event until we work with it
        err_code = clRetainEvent(event);
        if(err_code != CL_SUCCESS){
            std::stringstream msg;
            msg << "Failure reteaning the event for \"" <<
                (*it)->name() << "\" variable in \"" <<
                name() << "\" tool." << std::endl;
            LOG(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL execution error");
        }
        _events.push_back(event);            
    }

    return _events;
}

}}  // namespace
