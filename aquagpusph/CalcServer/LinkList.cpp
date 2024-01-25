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
 * @brief link-list based neighbours location algorithm.
 * (See Aqua::CalcServer::LinkList for details)
 * @note Hardcoded versions of the files CalcServer/LinkList.cl.in and
 * CalcServer/LinkList.hcl.in are internally included as a text array.
 */

#include "aquagpusph/AuxiliarMethods.hpp"
#include "aquagpusph/InputOutput/Logger.hpp"
#include "CalcServer.hpp"
#include "LinkList.hpp"
#include "SetScalar.hpp"
#include "Kernel.hpp"
#include <algorithm>

namespace Aqua {
namespace CalcServer {

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#include "aquagpusph/CalcServer/LinkList.hcl"
#include "aquagpusph/CalcServer/LinkList.cl"
#endif
std::string LINKLIST_INC = xxd2string(LinkList_hcl_in, LinkList_hcl_in_len);
std::string LINKLIST_SRC = xxd2string(LinkList_cl_in, LinkList_cl_in_len);

LinkList::LinkList(const std::string tool_name,
                   const std::string input,
                   bool once)
  : Tool(tool_name, once)
  , _input_name(input)
  , _cell_length(0.f)
  , _min_pos(NULL)
  , _max_pos(NULL)
  , _ihoc(NULL)
  , _ihoc_lws(0)
  , _ihoc_gws(0)
  , _icell(NULL)
  , _icell_lws(0)
  , _icell_gws(0)
  , _ll(NULL)
  , _ll_lws(0)
  , _ll_gws(0)
{
	_min_pos = new Reduction(tool_name + "->Min. Pos.",
	                         input,
	                         "r_min",
	                         "c = min(a, b);",
	                         "VEC_INFINITY");
	_max_pos = new Reduction(tool_name + "->Max. Pos.",
	                         input,
	                         "r_max",
	                         "c = max(a, b);",
	                         "-VEC_INFINITY");
	_sort = new RadixSort(tool_name + "->Radix-Sort");
	Profiler::substages({ new ScalarProfile("n_cells", this),
	                      new EventProfile("icell", this),
	                      new EventProfile("ihoc", this),
	                      new EventProfile("link-list", this) });
}

LinkList::~LinkList()
{
	if (_min_pos)
		delete _min_pos;
	_min_pos = NULL;
	if (_max_pos)
		delete _max_pos;
	_max_pos = NULL;
	if (_sort)
		delete _sort;
	_sort = NULL;
	if (_ihoc)
		clReleaseKernel(_ihoc);
	_ihoc = NULL;
	if (_icell)
		clReleaseKernel(_icell);
	_icell = NULL;
	if (_ll)
		clReleaseKernel(_ll);
	_ll = NULL;
	for (auto arg : _ihoc_args) {
		free(arg);
	}
	_ihoc_args.clear();
	for (auto arg : _icell_args) {
		free(arg);
	}
	_icell_args.clear();
	for (auto arg : _ll_args) {
		free(arg);
	}
	_ll_args.clear();
}

void
LinkList::setup()
{
	InputOutput::Variables* vars = CalcServer::singleton()->variables();

	std::ostringstream msg;
	msg << "Loading the tool \"" << name() << "\"..." << std::endl;
	LOG(L_INFO, msg.str());

	Tool::setup();

	// Setup the reduction tools
	_min_pos->setup();
	_max_pos->setup();

	// Compute the cells length
	InputOutput::Variable* s = vars->get("support");
	InputOutput::Variable* h = vars->get("h");
	_cell_length = *(float*)s->get() * *(float*)h->get();

	// Setup the kernels
	setupOpenCL();

	// Setup the radix-sort
	_sort->setup();

	setDependencies({ _input_name, "N", "n_radix", "support", "h" },
	                { "r_min", "r_max", "ihoc", "icell", "n_cells" });

	// This tool shall mark ihoc variable as reallocatable
	InputOutput::ArrayVariable* ihoc =
		(InputOutput::ArrayVariable*)getOutputDependencies()[2];
	ihoc->reallocatable(true);
}

cl_event
LinkList::_execute(const std::vector<cl_event> events)
{
	cl_int err_code;
	cl_event trigger, event;
	CalcServer* C = CalcServer::singleton();
	InputOutput::Variables* vars = C->variables();

	auto r_min = getOutputDependencies()[0];
	auto r_max = getOutputDependencies()[1];
	auto ihoc = getOutputDependencies()[2];
	auto icell = getOutputDependencies()[3];
	auto n_cells = getOutputDependencies()[4];

	// Reduction steps to find maximum and minimum position
	_min_pos->execute();
	_max_pos->execute();

	// Compute the number of cells and allocate ihoc accordingly
	const cl_event ncells_events[2] = { r_min->getWritingEvent(),
		                                r_max->getWritingEvent() };
	err_code = clWaitForEvents(2, ncells_events);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure waiting for the reductions on tool \"" << name()
		    << "\"." << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}

	dynamic_cast<ScalarProfile*>(Profiler::substages()[0])->start();
	nCells();
	allocate();

	// Set the new allocated variables
	setVariables();
	dynamic_cast<ScalarProfile*>(Profiler::substages()[0])->end();

	// Compute the cell of each particle
	err_code = clEnqueueNDRangeKernel(C->command_queue(),
	                                  _icell,
	                                  1,
	                                  NULL,
	                                  &_icell_gws,
	                                  &_icell_lws,
	                                  events.size(),
	                                  events.data(),
	                                  &event);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure executing \"iCell\" from tool \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}
	{
		auto profiler =
		    dynamic_cast<EventProfile*>(Profiler::substages()[1]);
		profiler->start(event);
		profiler->end(event);
	}

	// Time to sort the icell array
	icell->setWritingEvent(event);
	n_cells->addReadingEvent(event);
	err_code = clReleaseEvent(event);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure releasing transactional \"iCell\" event from tool \""
		    << name() << "\"." << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}

	// Sort the particles from the cells
	_sort->execute();

	// Now our transactional event is the one coming from the sorting algorithm
	// Such a new event can be taken from the icell dependency
	// This transactional event SHALL NOT BE RELEASED. It is automagically
	// destroyed when no more variables use it
	auto event_wait = icell->getWritingEvent();

	// Compute the head of cells
	err_code = clEnqueueNDRangeKernel(C->command_queue(),
	                                  _ihoc,
	                                  1,
	                                  NULL,
	                                  &_ihoc_gws,
	                                  &_ihoc_lws,
	                                  1,
	                                  &event_wait,
	                                  &event);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure executing \"iHoc\" from tool \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}
	event_wait = event;
	{
		auto profiler =
		    dynamic_cast<EventProfile*>(Profiler::substages()[2]);
		profiler->start(event);
		profiler->end(event);
	}

	err_code = clEnqueueNDRangeKernel(C->command_queue(),
	                                  _ll,
	                                  1,
	                                  NULL,
	                                  &_ll_gws,
	                                  &_ll_lws,
	                                  1,
	                                  &event_wait,
	                                  &event);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure executing \"linkList\" from tool \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}
	err_code = clReleaseEvent(event_wait);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure releasing transactional \"linkList\" event from tool \""
		    << name() << "\"." << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL execution error");
	}
	{
		auto profiler =
		    dynamic_cast<EventProfile*>(Profiler::substages()[3]);
		profiler->start(event);
		profiler->end(event);
	}

	return event;
}

void
LinkList::nCells()
{
	vec pos_min, pos_max;
	InputOutput::Variables* vars = CalcServer::singleton()->variables();

	if (!_cell_length) {
		std::stringstream msg;
		msg << "Zero cell length detected in the tool \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		throw std::runtime_error("Invalid number of cells");
	}

	pos_min = *(vec*)vars->get("r_min")->get_async();
	pos_max = *(vec*)vars->get("r_max")->get_async();

	_n_cells.x = (unsigned int)((pos_max.x - pos_min.x) / _cell_length) + 6;
	_n_cells.y = (unsigned int)((pos_max.y - pos_min.y) / _cell_length) + 6;
#ifdef HAVE_3D
	_n_cells.z = (unsigned int)((pos_max.z - pos_min.z) / _cell_length) + 6;
#else
	_n_cells.z = 1;
#endif
	_n_cells.w = _n_cells.x * _n_cells.y * _n_cells.z;
}

void
LinkList::allocate()
{
	uivec4 n_cells;
	cl_int err_code;
	cl_event event;
	CalcServer* C = CalcServer::singleton();
	InputOutput::Variables* vars = C->variables();

	auto n_cells_var = getOutputDependencies()[4];
	if (n_cells_var->type().compare("uivec4")) {
		std::stringstream msg;
		msg << "\"n_cells\" has and invalid type for \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		msg.str("");
		msg << "\tVariable \"n_cells\" type is \"" << n_cells_var->type()
		    << "\", while \"uivec4\" was expected" << std::endl;
		LOG0(L_DEBUG, msg.str());
		throw std::runtime_error("Invalid n_cells type");
	}

	n_cells = *(uivec4*)n_cells_var->get_async();

	if (_n_cells.w <= n_cells.w) {
		n_cells.x = _n_cells.x;
		n_cells.y = _n_cells.y;
		n_cells.z = _n_cells.z;
		n_cells_var->set_async(&n_cells);
		return;
	}

	// We have no alternative, we must sync here
	auto ihoc_var = getOutputDependencies()[2];
	cl_mem mem = *(cl_mem*)ihoc_var->get_async();
	if (mem)
		clReleaseMemObject(mem);
	mem = NULL;

	mem = clCreateBuffer(C->context(),
	                     CL_MEM_READ_WRITE,
	                     _n_cells.w * sizeof(cl_uint),
	                     NULL,
	                     &err_code);
	if (err_code != CL_SUCCESS) {
		std::stringstream msg;
		msg << "Failure allocating " << _n_cells.w * sizeof(cl_uint)
		    << " bytes on the device memory for tool \"" << name() << "\"."
		    << std::endl;
		LOG(L_ERROR, msg.str());
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL allocation error");
	}

	n_cells_var->set_async(&_n_cells);
	ihoc_var->set_async(&mem);
	_ihoc_gws = roundUp(_n_cells.w, _ihoc_lws);
}

void
LinkList::setVariables()
{
	unsigned int i;
	cl_int err_code;
	InputOutput::Variables* vars = CalcServer::singleton()->variables();

	const char* _ihoc_vars[3] = { "ihoc", "N", "n_cells" };
	for (i = 0; i < 3; i++) {
		InputOutput::Variable* var = vars->get(_ihoc_vars[i]);
		if (!memcmp(var->get_async(), _ihoc_args.at(i), var->typesize())) {
			continue;
		}
		err_code = clSetKernelArg(_ihoc, i, var->typesize(), var->get_async());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure setting the variable \"" << _ihoc_vars[i]
			    << "\" to the tool \"" << name() << "\" (\"iHoc\")."
			    << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		memcpy(_ihoc_args.at(i), var->get_async(), var->typesize());
	}

	const char* _icell_vars[8] = {
		"icell", _input_name.c_str(), "N", "n_radix",
		"r_min", "support",           "h", "n_cells"
	};
	for (i = 0; i < 8; i++) {
		InputOutput::Variable* var = vars->get(_icell_vars[i]);
		if (!memcmp(var->get_async(), _icell_args.at(i), var->typesize())) {
			continue;
		}
		err_code = clSetKernelArg(_icell, i, var->typesize(), var->get_async());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure setting the variable \"" << _ihoc_vars[i]
			    << "\" to the tool \"" << name() << "\" (\"iCell\")."
			    << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		memcpy(_icell_args.at(i), var->get_async(), var->typesize());
	}

	const char* _ll_vars[3] = { "icell", "ihoc", "N" };
	for (i = 0; i < 3; i++) {
		InputOutput::Variable* var = vars->get(_ll_vars[i]);
		if (!memcmp(var->get_async(), _ll_args.at(i), var->typesize())) {
			continue;
		}
		err_code = clSetKernelArg(_ll, i, var->typesize(), var->get_async());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure setting the variable \"" << _ihoc_vars[i]
			    << "\" to the tool \"" << name() << "\" (\"linkList\")."
			    << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		memcpy(_ll_args.at(i), var->get_async(), var->typesize());
	}
}

void
LinkList::setupOpenCL()
{
	unsigned int i;
	uivec4 n_cells;
	unsigned int n_radix, N;
	cl_int err_code;
	CalcServer* C = CalcServer::singleton();
	InputOutput::Variables* vars = C->variables();

	// Create a header for the source code where the operation will be placed
	std::ostringstream source;
	source << LINKLIST_INC << LINKLIST_SRC;

	std::vector<cl_kernel> kernels =
	    compile(source.str(), { "iHoc", "iCell", "linkList" });
	_ihoc = kernels.at(0);
	_icell = kernels.at(1);
	_ll = kernels.at(2);

	err_code = clGetKernelWorkGroupInfo(_ihoc,
	                                    C->device(),
	                                    CL_KERNEL_WORK_GROUP_SIZE,
	                                    sizeof(size_t),
	                                    &_ihoc_lws,
	                                    NULL);
	if (err_code != CL_SUCCESS) {
		LOG(L_ERROR, "Failure querying the work group size (\"iHoc\").\n");
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL error");
	}
	if (_ihoc_lws < __CL_MIN_LOCALSIZE__) {
		LOG(L_ERROR, "insufficient local memory for \"iHoc\".\n");
		std::stringstream msg;
		msg << "\t" << _ihoc_lws
		    << " local work group size with __CL_MIN_LOCALSIZE__="
		    << __CL_MIN_LOCALSIZE__ << std::endl;
		LOG0(L_DEBUG, msg.str());
		throw std::runtime_error("OpenCL error");
	}
	n_cells = *(uivec4*)vars->get("n_cells")->get();
	_ihoc_gws = roundUp(n_cells.w, _ihoc_lws);
	const char* _ihoc_vars[3] = { "ihoc", "N", "n_cells" };
	for (i = 0; i < 3; i++) {
		err_code = clSetKernelArg(_ihoc,
		                          i,
		                          vars->get(_ihoc_vars[i])->typesize(),
		                          vars->get(_ihoc_vars[i])->get());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure sending \"" << _ihoc_vars[i]
			    << "\" argument to \"iHoc\"." << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		_ihoc_args.push_back(malloc(vars->get(_ihoc_vars[i])->typesize()));
		memcpy(_ihoc_args.at(i),
		       vars->get(_ihoc_vars[i])->get(),
		       vars->get(_ihoc_vars[i])->typesize());
	}

	err_code = clGetKernelWorkGroupInfo(_icell,
	                                    C->device(),
	                                    CL_KERNEL_WORK_GROUP_SIZE,
	                                    sizeof(size_t),
	                                    &_icell_lws,
	                                    NULL);
	if (err_code != CL_SUCCESS) {
		LOG(L_ERROR, "Failure querying the work group size (\"iCell\").\n");
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL error");
	}
	if (_icell_lws < __CL_MIN_LOCALSIZE__) {
		LOG(L_ERROR, "insufficient local memory for \"iCell\".\n");
		std::stringstream msg;
		msg << "\t" << _icell_lws
		    << " local work group size with __CL_MIN_LOCALSIZE__="
		    << __CL_MIN_LOCALSIZE__ << std::endl;
		LOG0(L_DEBUG, msg.str());
		throw std::runtime_error("OpenCL error");
	}
	n_radix = *(unsigned int*)vars->get("n_radix")->get();
	_icell_gws = roundUp(n_radix, _icell_lws);
	const char* _icell_vars[8] = {
		"icell", _input_name.c_str(), "N", "n_radix",
		"r_min", "support",           "h", "n_cells"
	};
	for (i = 0; i < 8; i++) {
		err_code = clSetKernelArg(_icell,
		                          i,
		                          vars->get(_icell_vars[i])->typesize(),
		                          vars->get(_icell_vars[i])->get());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure sending \"" << _icell_vars[i]
			    << "\" argument to \"iCell\"." << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		_icell_args.push_back(malloc(vars->get(_icell_vars[i])->typesize()));
		memcpy(_icell_args.at(i),
		       vars->get(_icell_vars[i])->get(),
		       vars->get(_icell_vars[i])->typesize());
	}

	err_code = clGetKernelWorkGroupInfo(_ll,
	                                    C->device(),
	                                    CL_KERNEL_WORK_GROUP_SIZE,
	                                    sizeof(size_t),
	                                    &_ll_lws,
	                                    NULL);
	if (err_code != CL_SUCCESS) {
		LOG(L_ERROR, "Failure querying the work group size (\"linkList\").\n");
		InputOutput::Logger::singleton()->printOpenCLError(err_code);
		throw std::runtime_error("OpenCL error");
	}
	if (_ll_lws < __CL_MIN_LOCALSIZE__) {
		LOG(L_ERROR, "insufficient local memory for \"linkList\".\n");
		std::stringstream msg;
		msg << "\t" << _ll_lws
		    << " local work group size with __CL_MIN_LOCALSIZE__="
		    << __CL_MIN_LOCALSIZE__ << std::endl;
		LOG0(L_DEBUG, msg.str());
		throw std::runtime_error("OpenCL error");
	}
	N = *(unsigned int*)vars->get("N")->get();
	_ll_gws = roundUp(N, _ll_lws);
	const char* _ll_vars[3] = { "icell", "ihoc", "N" };
	for (i = 0; i < 3; i++) {
		err_code = clSetKernelArg(_ll,
		                          i,
		                          vars->get(_ll_vars[i])->typesize(),
		                          vars->get(_ll_vars[i])->get());
		if (err_code != CL_SUCCESS) {
			std::stringstream msg;
			msg << "Failure sending \"" << _ll_vars[i]
			    << "\" argument to \"iCell\"." << std::endl;
			LOG(L_ERROR, msg.str());
			InputOutput::Logger::singleton()->printOpenCLError(err_code);
			throw std::runtime_error("OpenCL error");
		}
		_ll_args.push_back(malloc(vars->get(_ll_vars[i])->typesize()));
		memcpy(_ll_args.at(i),
		       vars->get(_ll_vars[i])->get(),
		       vars->get(_ll_vars[i])->typesize());
	}
}

}
} // namespace