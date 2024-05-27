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
 * @brief Runtime output base class.
 * (See Aqua::CalcServer::Reports::Report for details)
 */

#include <sstream>
#include "aquagpusph/AuxiliarMethods.hpp"
#include "aquagpusph/InputOutput/Logger.hpp"
#include "aquagpusph/CalcServer/CalcServer.hpp"
#include "Report.hpp"

namespace Aqua {
namespace CalcServer {
namespace Reports {

Report::Report(const std::string tool_name,
               const std::string fields,
               unsigned int ipf,
               float fps)
  : Tool(tool_name)
  , _fields(fields)
  , _ipf(ipf)
  , _fps(fps)
  , _iter(0)
  , _t(0.f)
  , _user_event(NULL)

{
}

Report::~Report()
{
	_vars_per_line.clear();
	_vars.clear();
}

void
Report::setup()
{
	Tool::setup();
	processFields(_fields);
}

const std::string
Report::data(bool with_title, bool with_names, bool synced)
{
	unsigned int i, j, var_id = 0;

	std::stringstream data;

	// Create the title
	if (with_title) {
		data << name() << ":" << std::endl;
	}

	// Set the variable per lines
	for (i = 0; i < _vars_per_line.size(); i++) {
		for (j = 0; j < _vars_per_line.at(i); j++) {
			InputOutput::Variable* var = _vars.at(var_id);
			if (with_names) {
				data << var->name() << "=";
			}
			data << var->asString(synced) << " ";
			var_id++;
		}
		// Replace the trailing space by a line break
		data.seekp(-1, data.cur);
		data << std::endl;
	}

	_data = data.str();
	return _data;
}

void
Report::processFields(const std::string input)
{
	CalcServer* C = CalcServer::singleton();
	std::istringstream fields(input);
	std::string s;
	// The user may ask to break lines by semicolon usage. In such a case,
	// we are splitting the input and recursively calling this function with
	// each piece
	if (input.find(';') != std::string::npos) {
		while (getline(fields, s, ';')) {
			processFields(s);
		}
		return;
	}

	// Now we now that the fields should be process as a single line
	InputOutput::Variables* vars = C->variables();
	unsigned int vars_in_line = 0;
	std::istringstream subfields(replaceAllCopy(input, " ", ","));
	while (getline(subfields, s, ',')) {
		if (s == "")
			continue;
		InputOutput::Variable* var = vars->get(s);
		if (!var) {
			std::stringstream msg;
			msg << "The report \"" << name()
			    << "\" is asking the undeclared variable \"" << s << "\""
			    << std::endl;
			LOG(L_ERROR, msg.str());
			throw std::runtime_error("Invalid variable");
		}
		vars_in_line++;
		_vars.push_back(var);
	}
	_vars_per_line.push_back(vars_in_line);

	setInputDependencies(_vars);
}

bool
Report::mustUpdate()
{
	CalcServer* C = CalcServer::singleton();
	InputOutput::Variables* vars = C->variables();

	InputOutput::UIntVariable* iter_var =
	    (InputOutput::UIntVariable*)vars->get("iter");
	unsigned int iter = *(unsigned int*)iter_var->get();
	InputOutput::FloatVariable* time_var =
	    (InputOutput::FloatVariable*)vars->get("t");
	float t = *(float*)time_var->get();

	if (_ipf > 0) {
		if (!_iter || (iter - _iter >= _ipf)) {
			_iter = iter;
			_t = t;
			return true;
		}
	}
	if (_fps > 0.f) {
		if ((_t == 0.f) || (t - _t >= 1.f / _fps)) {
			_iter = iter;
			_t = t;
			return true;
		}
	}
	return false;
}

cl_event
Report::setCallback(const std::vector<cl_event> events,
                    void(CL_CALLBACK* cb)(cl_event, cl_int, void*))
{
	cl_int err_code;
	cl_event event;
	auto C = CalcServer::singleton();
	try {
		event = C->marker(C->command_queue(), events);
	} catch (std::runtime_error e) {
		LOG(L_ERROR, std::string("While setting the trigger in tool \"") +
		             name() + "\".\n");
	}

	// Now we create a user event that we will set as completed when we already
	// readed the input dependencies
	_user_event = clCreateUserEvent(C->context(), &err_code);
	CHECK_OCL_OR_THROW(
	    err_code,
	    std::string("Failure creating the user event in tool \"") + name() +
	        "\".");

	// So it is time to register our callback on our trigger
	err_code = clRetainEvent(_user_event);
	CHECK_OCL_OR_THROW(
	    err_code,
	    std::string("Failure retaining the user event in tool \"") + name() +
	        "\".");
	err_code = clSetEventCallback(event, CL_COMPLETE, cb, this);
	CHECK_OCL_OR_THROW(
	    err_code,
	    std::string("Failure registering the callback in tool \"") + name() +
	        "\".");

	return _user_event;
}

} // ::Reports
} // ::CalcServer
} // ::Aqua
