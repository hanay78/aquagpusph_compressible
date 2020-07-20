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
 * @brief UnSort Recover the original id of each particle.
 * (See Aqua::CalcServer::UnSort for details)
 * @note Hardcoded versions of the files CalcServer/UnSort.cl.in and
 * CalcServer/UnSort.hcl.in are internally included as a text array.
 */

#include <AuxiliarMethods.h>
#include <InputOutput/Logger.h>
#include <CalcServer/UnSort.h>
#include <CalcServer.h>

namespace Aqua{ namespace CalcServer{

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#include "CalcServer/UnSort.hcl"
#include "CalcServer/UnSort.cl"
#endif
std::string UNSORT_INC = xxd2string(UnSort_hcl_in, UnSort_hcl_in_len);
std::string UNSORT_SRC = xxd2string(UnSort_cl_in, UnSort_cl_in_len);


UnSort::UnSort(const std::string name,
               const std::string var_name,
               const std::string permutations_name,
               bool once)
    : Tool(name, once)
    , _var_name(var_name)
    , _perms_name(permutations_name)
    , _var(NULL)
    , _id_var(NULL)
    , _input(NULL)
    , _id_input(NULL)
    , _output(NULL)
    , _kernel(NULL)
    , _global_work_size(0)
    , _local_work_size(0)
    , _n(0)
{
}

UnSort::~UnSort()
{
    if(_output) clReleaseMemObject(_output); _output=NULL;
    if(_kernel) clReleaseKernel(_kernel); _kernel=NULL;
}

void UnSort::setup()
{
    Tool::setup();
    variables();
    setupMem();

    _id_input = *(cl_mem*)_id_var->get();
    _input = *(cl_mem*)_var->get();
    _n = _id_var->size() / InputOutput::Variables::typeToBytes(_id_var->type());
    setupOpenCL();
}

cl_event UnSort::_execute(const std::vector<cl_event> events)
{
    unsigned int i;
    cl_int err_code;
    cl_event event;
    CalcServer *C = CalcServer::singleton();

    setVariables();

    cl_uint num_events_in_wait_list = events.size();
    const cl_event *event_wait_list = events.size() ? events.data() : NULL;

    err_code = clEnqueueNDRangeKernel(C->command_queue(),
                                      _kernel,
                                      1,
                                      NULL,
                                      &_global_work_size,
                                      &_local_work_size,
                                      num_events_in_wait_list,
                                      event_wait_list,
                                      &event);
    if(err_code != CL_SUCCESS) {
        std::stringstream msg;
        msg << "Failure executing the tool \"" <<
               name() << "\"." << std::endl;
        LOG(L_ERROR, msg.str());
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL execution error");
    }

    return event;
}

void UnSort::variables()
{
    CalcServer *C = CalcServer::singleton();
    InputOutput::Variables *vars = C->variables();
    if(!vars->get(_perms_name)){
        std::stringstream msg;
        msg << "The tool \"" << name()
            << "\" is asking the undeclared variable \"" << _perms_name << "\""
            << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Invalid variable");
    }
    if(vars->get(_perms_name)->type().compare("unsigned int*")){
        std::stringstream msg;
        msg << "The tool \"" << name()
            << "\" is asking the variable \"id\", which has an invalid type"
            << std::endl;
        LOG(L_ERROR, msg.str());
        msg.str("");
        msg << "\t\"unsigned int*\" was expected, but \""
            << vars->get(_perms_name)->type() << "\" was found." << std::endl;
        LOG0(L_DEBUG, msg.str());
        throw std::runtime_error("Invalid variable type");
    }
    _id_var = (InputOutput::ArrayVariable *)vars->get(_perms_name);

    if(!vars->get(_var_name)){
        std::stringstream msg;
        msg << "The tool \"" << name()
            << "\" is asking the undeclared variable \""
            << _var_name << "\"." << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Invalid variable");
    }
    if(vars->get(_var_name)->type().find('*') == std::string::npos){
        std::stringstream msg;
        msg << "The tool \"" << name()
            << "\" may not use a scalar variable (\""
            << _var_name << "\")." << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Invalid variable type");
    }
    _var = (InputOutput::ArrayVariable *)vars->get(_var_name);

    std::vector<InputOutput::Variable*> deps = {_id_var, _var};
    setDependencies(deps);
}

void UnSort::setupMem()
{
    cl_int err_code;
    size_t len_id, len_var;
    CalcServer *C = CalcServer::singleton();

    len_id = _id_var->size() / InputOutput::Variables::typeToBytes(_id_var->type());
    len_var = _var->size() / InputOutput::Variables::typeToBytes(_var->type());
    if(len_id > len_var){
        std::stringstream msg;
        msg << "Wrong variable length in the tool \"" << name()
            << "\"." << std::endl;
        LOG(L_ERROR, msg.str());
        msg.str("");
        msg << "\t\"" << _perms_name << "\" has length " << len_id << std::endl;
        LOG0(L_DEBUG, msg.str());
        msg.str("");
        msg << "\t\"" << _var_name << "\" has length " << len_var << std::endl;
        LOG0(L_DEBUG, msg.str());
        throw std::runtime_error("Invalid variable length");
    }

    _output = clCreateBuffer(C->context(),
                             CL_MEM_WRITE_ONLY,
                             len_id * InputOutput::Variables::typeToBytes(_var->type()),
                             NULL,
                               &err_code);
    if(err_code != CL_SUCCESS){
        std::stringstream msg;
        msg << "Failure allocating device memory in the tool \"" <<
               name() << "\"." << std::endl;
        LOG(L_ERROR, msg.str());
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL allocation error");
    }
    allocatedMemory(len_id * InputOutput::Variables::typeToBytes(_var->type()));
}

void UnSort::setupOpenCL()
{
    cl_int err_code;
    CalcServer *C = CalcServer::singleton();

    std::ostringstream source;
    source << UNSORT_INC << UNSORT_SRC;
    std::ostringstream flags;
    if(!_var->type().compare("unsigned int*")){
        // Spaces are not a good business to define a variable
        flags << "-DT=uint";
    }
    else{
        std::string t = trimCopy(_var->type());
        t.pop_back();  // Remove the asterisk
        flags << "-DT=" << t;
    }
    _kernel = compile_kernel(source.str(), "unsort", flags.str());

    err_code = clGetKernelWorkGroupInfo(_kernel,
                                        C->device(),
                                        CL_KERNEL_WORK_GROUP_SIZE,
                                        sizeof(size_t),
                                        &_local_work_size,
                                        NULL);
    if(err_code != CL_SUCCESS) {
        LOG(L_ERROR, "Failure querying the work group size.\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
    if(_local_work_size < __CL_MIN_LOCALSIZE__){
        std::stringstream msg;
        LOG(L_ERROR, "UnSort cannot be performed.\n");
        msg << "\t" << _local_work_size
            << " elements can be executed, but __CL_MIN_LOCALSIZE__="
            << __CL_MIN_LOCALSIZE__ << std::endl;
        LOG0(L_DEBUG, msg.str());
        throw std::runtime_error("OpenCL error");
    }

    _global_work_size = roundUp(_n, _local_work_size);
    err_code = clSetKernelArg(_kernel,
                              0,
                              _id_var->typesize(),
                              _id_var->get());
    if(err_code != CL_SUCCESS){
        LOG(L_ERROR, "Failure sending the IDs argument\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
    err_code = clSetKernelArg(_kernel,
                              1,
                              _var->typesize(),
                              _var->get());
    if(err_code != CL_SUCCESS){
        LOG(L_ERROR, "Failure sending the input array argument\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
    err_code = clSetKernelArg(_kernel,
                              2,
                              sizeof(cl_mem),
                              (void*)&_output);
    if(err_code != CL_SUCCESS){
        LOG(L_ERROR, "Failure sending the output array argument\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
    err_code = clSetKernelArg(_kernel,
                              3,
                              sizeof(unsigned int),
                              (void*)&_n);
    if(err_code != CL_SUCCESS){
        LOG(L_ERROR, "Failure sending the array size argument\n");
        InputOutput::Logger::singleton()->printOpenCLError(err_code);
        throw std::runtime_error("OpenCL error");
    }
}

void UnSort::setVariables()
{
    cl_int err_code;

    if(_id_input != *(cl_mem*)_id_var->get()){
        err_code = clSetKernelArg(_kernel,
                                  0,
                                  _id_var->typesize(),
                                  _id_var->get());
        if(err_code != CL_SUCCESS) {
            std::stringstream msg;
            msg << "Failure setting the variable \"" << _id_var->name()
                << "\" to the tool \"" << name() << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL error");
        }
        _id_input = *(cl_mem *)_id_var->get();
    }
    if(_input != *(cl_mem*)_var->get()){
        err_code = clSetKernelArg(_kernel,
                                  1,
                                  _var->typesize(),
                                  _var->get());
        if(err_code != CL_SUCCESS) {
            std::stringstream msg;
            msg << "Failure setting the variable \"" << _var->name()
                << "\" to the tool \"" << name() << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            InputOutput::Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL error");
        }
        _input = *(cl_mem *)_var->get();
    }
}

}}  // namespaces
