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
 * @brief Set a scalar variable.
 * (See Aqua::CalcServer::SetScalar for details)
 */

#ifndef SETSCALAR_H_INCLUDED
#define SETSCALAR_H_INCLUDED

#include <CalcServer.h>
#include <CalcServer/Tool.h>
#include <vector>

namespace Aqua {
namespace CalcServer {

/** @class ScalarExpression SetScalar.h CalcServer/SetScalar.h
 * @brief Base class for tools that has to evaluate a scalar expression.
 *
 * This tool evaluates the expression asynchronously
 */
class ScalarExpression : public Aqua::CalcServer::Tool
{
  public:
	/** @brief Constructor.
	 * @param name Tool name.
	 * @param expr Expression to evaluate.
	 * @param type Type of the output value. It can be modified later.
	 * @param once Run this tool just once. Useful to make initializations.
	 */
	ScalarExpression(const std::string name,
	                 const std::string expr,
	                 const std::string type = "float",
	                 bool once = false);

	/// Destructor.
	~ScalarExpression();

	/** @brief Initialize the tool.
	 */
	virtual void setup();

	/** @brief Get the output typesize
	 * @return The output typesize
	 */
	inline const std::string getOutputType() const { return _output_type; }

	/** @brief Set the output typesize
	 * @return The output typesize
	 * @warning This function will reallocate memory, discarding stored results
	 * from eventual previous evaluations
	 */
	void setOutputType(const std::string type);

	/** @brief Get the expression to evaluate
	 * @return The expression
	 */
	inline const std::string getExpression() const { return _value; }

	/** @brief Get the stored output value
	 * @return The output value memory
	 */
	inline const void* getValue() const { return _output; }

	/** @brief Get the main tool event
	 * @return The event
	 */
	inline cl_event getEvent() const { return _event; }

	/** @brief Evaluate the expression and store the value
	 *
	 * Also the execution elapsed time is measured and the event is set as
	 * completed.
	 *
	 * This function is actually a closure for _solve() function, that can be
	 * overloaded to extent the functionality. E.g. to populate the output
	 * value on a variable
	 */
	void solve();

  protected:
	/** Execute the tool
	 * @param events List of events that shall be waited before safe execution
	 * @return OpenCL event to be waited before accessing the dependencies
	 */
	cl_event _execute(const std::vector<cl_event> events);

	/** @brief Evaluate the expression and store the value
	 *
	 * This function can be overloaded to extent the functionality. E.g. to
	 * populate the output value on a variable
	 */
	virtual void _solve();

  private:
	/** @brief Get the input variables from the expression
	 */
	void variables();

	/// Expression to evaluate
	std::string _value;

	/// Input variables
	std::vector<InputOutput::Variable*> _in_vars;
	/// Output
	void* _output;
	/// Output size
	std::string _output_type;

	/// Convenient storage of the event to make easier to work with the
	/// callback
	cl_event _event;
};

/** @class SetScalar SetScalar.h CalcServer/SetScalar.h
 * @brief Set a scalar variable.
 */
class SetScalar final : public Aqua::CalcServer::ScalarExpression
{
  public:
	/** @brief Constructor.
	 * @param name Tool name.
	 * @param var_name Variable to set.
	 * @param value Value to set.
	 * @param once Run this tool just once. Useful to make initializations.
	 */
	SetScalar(const std::string name,
	          const std::string var_name,
	          const std::string value,
	          bool once = false);

	/// Destructor.
	~SetScalar();

	/** @brief Initialize the tool.
	 */
	void setup();

	/** @brief Get the output variable
	 * @return The output variable
	 */
	inline InputOutput::Variable* getOutputVariable() const { return _var; }

  protected:
	/** @brief Evaluate the expression and populate the value on
	 * the varaible Aqua::CalcServer::ScalarExpression::_var
	 */
	void _solve();

  private:
	/** @brief Get a variable
	 *
	 * The variable must exist and it shall not be an array
	 * @param name Variable name
	 * @return The variable
	 * @throw std::runtime_error If either the variable does not exist, or it
	 * has an invalid type
	 */
	InputOutput::Variable* variable(const std::string& name) const;

	/// Output variable name
	std::string _var_name;

	/// Output variable
	InputOutput::Variable* _var;
};

}
} // namespace

#endif // SETSCALAR_H_INCLUDED
