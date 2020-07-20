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
 * @brief Particles VTK data files loader/saver.
 * (See Aqua::InputOutput::VTK for details)
 */

#include <sphPrerequisites.h>

#ifdef HAVE_VTK

#include <unistd.h>
#include <signal.h>

#include <InputOutput/VTK.h>
#include <InputOutput/Logger.h>
#include <ProblemSetup.h>
#include <CalcServer.h>
#include <AuxiliarMethods.h>

#include <vector>
static std::vector<std::string> cpp_str;
static std::vector<XMLCh*> xml_str;
static std::vector<xercesc::XercesDOMParser*> parsers;

static std::string xmlTranscode(const XMLCh *txt)
{
    std::string str = xercesc::XMLString::transcode(txt);
    cpp_str.push_back(str);
    return str;
}

static XMLCh *xmlTranscode(const std::string txt)
{
    XMLCh *str = xercesc::XMLString::transcode(txt.c_str());
    xml_str.push_back(str);
    return str;
}

static void xmlClear()
{
    unsigned int i;
    cpp_str.clear();
    for(auto str : xml_str){
        xercesc::XMLString::release(&str);
    }
    xml_str.clear();
    for(auto parser : parsers){
        delete parser;
    }
    parsers.clear();
}

#ifdef xmlS
    #undef xmlS
#endif // xmlS
#define xmlS(txt) xmlTranscode(txt)

#ifdef xmlAttribute
    #undef xmlAttribute
#endif
#define xmlAttribute(elem, att) xmlS( elem->getAttribute(xmlS(att)) )

#ifdef xmlHasAttribute
    #undef xmlHasAttribute
#endif
#define xmlHasAttribute(elem, att) elem->hasAttribute(xmlS(att))

#ifndef REQUESTED_FIELDS
    #ifdef HAVE_3D
        #define REQUESTED_FIELDS 17
    #else
        #define REQUESTED_FIELDS 13
    #endif
#endif // REQUESTED_FIELDS

using namespace xercesc;

namespace Aqua{ namespace InputOutput{

VTK::VTK(ProblemSetup& sim_data,
         unsigned int iset,
         unsigned int first,
         unsigned int n_in)
    : Particles(sim_data, iset, first, n_in)
    , _next_file_index(0)
{
    if(n() == 0) {
        n(compute_n());
    }
}

VTK::~VTK()
{
    waitForSavers();
}

void VTK::load()
{
    unsigned int i, j, k, n, N, progress;
    int aux;
    cl_int err_code;
    CalcServer::CalcServer *C = CalcServer::CalcServer::singleton();

    loadDefault();

    std::ostringstream msg;
    msg << "Loading particles from VTK file \""
             <<  simData().sets.at(setId())->inputPath()
             << "\"..." << std::endl;
    LOG(L_INFO, msg.str());

    vtkSmartPointer<vtkXMLUnstructuredGridReader> f =
        vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();

    if(!f->CanReadFile(simData().sets.at(setId())->inputPath().c_str())){
        LOG(L_ERROR, "The file cannot be read.\n");
        throw std::runtime_error("Failure reading file");
    }

    f->SetFileName(simData().sets.at(setId())->inputPath().c_str());
    f->Update();

    vtkSmartPointer<vtkUnstructuredGrid> grid = f->GetOutput();

    // Assert that the number of particles is right
    n = bounds().y - bounds().x;
    N = (unsigned int)grid->GetNumberOfPoints();
    if( n != N){
        std::ostringstream msg;
        msg << "Expected " << n << " particles, but the file contains just "
            << N << " ones." << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Invalid number of particles in file");
    }

    // Check the fields to read
    std::vector<std::string> fields = simData().sets.at(setId())->inputFields();
    if(!fields.size()){
        LOG0(L_ERROR, "0 fields were set to be read from the file.\n");
        throw std::runtime_error("No fields have been marked to read");
    }
    bool have_r = false;
    for(auto field : fields){
        if(!field.compare("r")){
            have_r = true;
            break;
        }
    }
    if(!have_r){
        LOG0(L_ERROR, "\"r\" field was not set to be read from the file.\n");
        throw std::runtime_error("\"r\" field is mandatory");
    }

    // Setup an storage
    std::vector<void*> data;
    Variables *vars = C->variables();
    for(auto field : fields){
        if(!vars->get(field)){
            std::ostringstream msg;
            msg << "Undeclared variable \"" << field
                << "\" set to be read." << std::endl;
            LOG(L_ERROR, msg.str());
            throw std::runtime_error("Invalid variable");
        }
        if(vars->get(field)->type().find('*') == std::string::npos){
            std::ostringstream msg;
            msg << "Can't read scalar variable \"" << field
                << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            throw std::runtime_error("Invalid variable type");
        }
        ArrayVariable *var = (ArrayVariable*)vars->get(field);
        size_t typesize = vars->typeToBytes(var->type());
        size_t len = var->size() / typesize;
        if(len < bounds().y){
            std::ostringstream msg;
            msg << "Array variable \"" << field
                << "\" is not long enough." << std::endl;
            LOG(L_ERROR, msg.str());
            throw std::runtime_error("Invalid variable length");
        }
        void *store = malloc(typesize * n);
        if(!store){
            std::ostringstream msg;
            msg << "Failure allocating " << typesize * n
                << "bytes for variable \"" << field
                << "\"." << std::endl;
            LOG(L_ERROR, msg.str());
            throw std::bad_alloc();
        }
        data.push_back(store);
    }

    progress = -1;
    vtkSmartPointer<vtkPoints> vtk_points = grid->GetPoints();
    vtkSmartPointer<vtkPointData> vtk_data = grid->GetPointData();
    for(i = 0; i < n; i++){
        for(j = 0; j < fields.size(); j++){
            if(!fields.at(j).compare("r")){
                double *vect = vtk_points->GetPoint(i);
                vec *ptr = (vec*)data.at(j);
                ptr[i].x = vect[0];
                ptr[i].y = vect[1];
                #ifdef HAVE_3D
                    ptr[i].z = vect[2];
                    ptr[i].w = 0.f;
                #endif
                continue;
            }
            ArrayVariable *var = (ArrayVariable*)vars->get(fields.at(j));
            size_t type_size = vars->typeToBytes(var->type());
            unsigned int n_components = vars->typeToN(var->type());
            if(var->type().find("unsigned int") != std::string::npos ||
               var->type().find("uivec") != std::string::npos) {
                vtkSmartPointer<vtkUnsignedIntArray> vtk_array =
                    (vtkUnsignedIntArray*)(vtk_data->GetArray(fields.at(j).c_str(), aux));
                for(k = 0; k < n_components; k++){
                    unsigned int component = vtk_array->GetComponent(i, k);
                    size_t offset = type_size * i + sizeof(unsigned int) * k;
                    memcpy((char*)data.at(j) + offset,
                           &component,
                           sizeof(unsigned int));
                }
            }
            else if(var->type().find("int") != std::string::npos ||
                    var->type().find("ivec") != std::string::npos) {
                vtkSmartPointer<vtkIntArray> vtk_array =
                    (vtkIntArray*)(vtk_data->GetArray(fields.at(j).c_str(), aux));
                for(k = 0; k < n_components; k++){
                    int component = vtk_array->GetComponent(i, k);
                    size_t offset = type_size * i + sizeof(int) * k;
                    memcpy((char*)data.at(j) + offset,
                           &component,
                           sizeof(int));
                }
            }
            else if(var->type().find("float") != std::string::npos ||
                    var->type().find("vec") != std::string::npos ||
                    var->type().find("matrix") != std::string::npos) {
                vtkSmartPointer<vtkFloatArray> vtk_array =
                    (vtkFloatArray*)(vtk_data->GetArray(fields.at(j).c_str(), aux));
                for(k = 0; k < n_components; k++){
                    float component = vtk_array->GetComponent(i, k);
                    size_t offset = type_size * i + sizeof(float) * k;
                    memcpy((char*)data.at(j) + offset,
                           &component,
                           sizeof(float));
                }
            }
        }
        if(progress != i * 100 / n){
            progress = i * 100 / n;
            if(!(progress % 10)){
                std::ostringstream msg;
                msg << "\t\t" << progress << "%" << std::endl;
                LOG(L_DEBUG, msg.str());
            }
        }
    }

    // Send the data to the server and release it
    for(i = 0; i < fields.size(); i++){
        ArrayVariable *var = (ArrayVariable*)vars->get(fields.at(i));
        size_t typesize = vars->typeToBytes(var->type());
        cl_mem mem = *(cl_mem*)var->get();
        err_code = clEnqueueWriteBuffer(C->command_queue(),
                                        mem,
                                        CL_TRUE,
                                        typesize * bounds().x,
                                        typesize * n,
                                        data.at(i),
                                        0,
                                        NULL,
                                        NULL);
        free(data.at(i)); data.at(i) = NULL;
        if(err_code != CL_SUCCESS){
            std::ostringstream msg;
            msg << "Failure sending variable \"" << fields.at(i)
                << "\" to the computational device." << std::endl;
            LOG(L_ERROR, msg.str());
            Logger::singleton()->printOpenCLError(err_code);
            throw std::runtime_error("OpenCL error");
        }
    }
    data.clear();
}

/** @brief Data structure to send the data to a parallel writer thread
 */
typedef struct{
    /// The field names
    std::vector<std::string> fields;
    /// Bounds of the particles index managed by this writer
    uivec2 bounds;
    /// Screen manager
    Logger *S;
    /// VTK arrays
    CalcServer::CalcServer *C;
    /// The data associated to each field
    std::vector<void*> data;
    /// The VTK file decriptor
    vtkXMLUnstructuredGridWriter *f;
}data_pthread;

/** @brief Parallel thread to write the data
 * @param data_void Input data of type data_pthread* (dynamically casted as
 * void*)
 */
void* save_pthread(void *data_void)
{
    unsigned int i, j;
    data_pthread *data = (data_pthread*)data_void;

    // Create storage arrays
    std::vector< vtkSmartPointer<vtkDataArray> > vtk_arrays;
    Variables *vars = data->C->variables();
    for(auto field : data->fields){
        if(!vars->get(field)){
            std::ostringstream msg;
            msg << "Can't save undeclared variable \"" << field
                << "\"." << std::endl;
            data->S->addMessage(L_ERROR, msg.str());
            for(auto d : data->data)
                free(d);
            data->data.clear();
            data->f->Delete();
            delete data; data=NULL;
            return NULL;
        }
        if(vars->get(field)->type().find('*') == std::string::npos){
            std::ostringstream msg;
            msg << "Can't save scalar variable \"" << field
                << "\"." << std::endl;
            data->S->addMessage(L_ERROR, msg.str());
            for(auto d : data->data)
                free(d);
            data->data.clear();
            data->f->Delete();
            delete data; data=NULL;
            return NULL;
        }
        ArrayVariable *var = (ArrayVariable*)vars->get(field);
        size_t typesize = vars->typeToBytes(var->type());
        size_t len = var->size() / typesize;
        if(len < data->bounds.y){
            std::ostringstream msg;
            msg << "Variable \"" << field
                << "\" is not long enough." << std::endl;
            data->S->addMessageF(L_ERROR, msg.str());
            for(auto d : data->data)
                free(d);
            data->data.clear();
            data->f->Delete();
            delete data; data=NULL;
            return NULL;
        }

        unsigned int n_components = vars->typeToN(var->type());
        if(var->type().find("unsigned int") != std::string::npos ||
           var->type().find("uivec") != std::string::npos) {
            vtkSmartPointer<vtkUnsignedIntArray> vtk_array =
                vtkSmartPointer<vtkUnsignedIntArray>::New();
            vtk_array->SetNumberOfComponents(n_components);
            vtk_array->SetName(field.c_str());
            vtk_arrays.push_back(vtk_array);
        }
        else if(var->type().find("int") != std::string::npos ||
                var->type().find("ivec") != std::string::npos) {
            vtkSmartPointer<vtkIntArray> vtk_array =
                vtkSmartPointer<vtkIntArray>::New();
            vtk_array->SetNumberOfComponents(n_components);
            vtk_array->SetName(field.c_str());
            vtk_arrays.push_back(vtk_array);
        }
        else if(var->type().find("float") != std::string::npos ||
                var->type().find("vec") != std::string::npos ||
                var->type().find("matrix") != std::string::npos) {
            vtkSmartPointer<vtkFloatArray> vtk_array =
                vtkSmartPointer<vtkFloatArray>::New();
            vtk_array->SetNumberOfComponents(n_components);
            vtk_array->SetName(field.c_str());
            vtk_arrays.push_back(vtk_array);
        }
    }

    vtkSmartPointer<vtkVertex> vtk_vertex;
    vtkSmartPointer<vtkPoints> vtk_points = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> vtk_cells = vtkSmartPointer<vtkCellArray>::New();

    for(i = 0; i < data->bounds.y - data->bounds.x; i++){
        for(j = 0; j < data->fields.size(); j++){
            if(!data->fields.at(j).compare("r")){
                vec *ptr = (vec*)(data->data.at(j));
                #ifdef HAVE_3D
                    vtk_points->InsertNextPoint(ptr[i].x, ptr[i].y, ptr[i].z);
                #else
                    vtk_points->InsertNextPoint(ptr[i].x, ptr[i].y, 0.f);
                #endif
                continue;
            }
            ArrayVariable *var = (ArrayVariable*)(
                vars->get(data->fields.at(j)));
            size_t typesize = vars->typeToBytes(var->type());
            unsigned int n_components = vars->typeToN(var->type());
            if(var->type().find("unsigned int") != std::string::npos ||
               var->type().find("uivec") != std::string::npos) {
                unsigned int vect[n_components];
                size_t offset = typesize * i;
                memcpy(vect,
                       (char*)(data->data.at(j)) + offset,
                       n_components * sizeof(unsigned int));
                vtkSmartPointer<vtkUnsignedIntArray> vtk_array =
                    (vtkUnsignedIntArray*)(vtk_arrays.at(j).GetPointer());
                #if VTK_MAJOR_VERSION < 7
                    vtk_array->InsertNextTupleValue(vect);
                #else
                    vtk_array->InsertNextTypedTuple(vect);
                #endif // VTK_MAJOR_VERSION
            }
            else if(var->type().find("int") != std::string::npos ||
                    var->type().find("ivec") != std::string::npos) {
                int vect[n_components];
                size_t offset = typesize * i;
                memcpy(vect,
                       (char*)(data->data.at(j)) + offset,
                       n_components * sizeof(int));
                vtkSmartPointer<vtkIntArray> vtk_array =
                    (vtkIntArray*)(vtk_arrays.at(j).GetPointer());
                #if VTK_MAJOR_VERSION < 7
                    vtk_array->InsertNextTupleValue(vect);
                #else
                    vtk_array->InsertNextTypedTuple(vect);
                #endif // VTK_MAJOR_VERSION
            }
            else if(var->type().find("float") != std::string::npos ||
                    var->type().find("vec") != std::string::npos ||
                    var->type().find("matrix") != std::string::npos) {
                float vect[n_components];
                size_t offset = typesize * i;
                memcpy(vect,
                       (char*)(data->data.at(j)) + offset,
                       n_components * sizeof(float));
                vtkSmartPointer<vtkFloatArray> vtk_array =
                    (vtkFloatArray*)(vtk_arrays.at(j).GetPointer());
                #if VTK_MAJOR_VERSION < 7
                    vtk_array->InsertNextTupleValue(vect);
                #else
                    vtk_array->InsertNextTypedTuple(vect);
                #endif // VTK_MAJOR_VERSION
            }
        }
        vtk_vertex = vtkSmartPointer<vtkVertex>::New();
        vtk_vertex->GetPointIds()->SetId(0, i);
        vtk_cells->InsertNextCell(vtk_vertex);
    }

    // Setup the unstructured grid
    vtkSmartPointer<vtkUnstructuredGrid> grid =
        vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(vtk_points);
    grid->SetCells(vtk_vertex->GetCellType(), vtk_cells);
    for(i = 0; i < data->fields.size(); i++){
        if(!data->fields.at(i).compare("r")){
            continue;
        }

        ArrayVariable *var = (ArrayVariable*)(
            vars->get(data->fields.at(i)));
        if(var->type().find("unsigned int") != std::string::npos ||
           var->type().find("uivec") != std::string::npos) {
            vtkSmartPointer<vtkUnsignedIntArray> vtk_array =
                (vtkUnsignedIntArray*)(vtk_arrays.at(i).GetPointer());
            grid->GetPointData()->AddArray(vtk_array);
        }
        else if(var->type().find("int") != std::string::npos ||
                var->type().find("ivec") != std::string::npos) {
            vtkSmartPointer<vtkIntArray> vtk_array =
                (vtkIntArray*)(vtk_arrays.at(i).GetPointer());
            grid->GetPointData()->AddArray(vtk_array);
        }
        else if(var->type().find("float") != std::string::npos ||
                var->type().find("vec") != std::string::npos ||
                var->type().find("matrix") != std::string::npos) {
            vtkSmartPointer<vtkFloatArray> vtk_array =
                (vtkFloatArray*)(vtk_arrays.at(i).GetPointer());
            grid->GetPointData()->AddArray(vtk_array);
        }
    }

    // Write file
    #if VTK_MAJOR_VERSION <= 5
        data->f->SetInput(grid);
    #else // VTK_MAJOR_VERSION
        data->f->SetInputData(grid);
    #endif // VTK_MAJOR_VERSION

    if(!data->f->Write()){
        data->S->addMessageF(L_ERROR,
            std::string("Failure writing \"") +
            data->f->GetFileName() + "\" VTK file.\n");
    }

    // Clean up
    for(auto d : data->data)
        free(d);
    data->data.clear();
    data->S->addMessageF(L_INFO,
        std::string("Wrote \"") + data->f->GetFileName() + "\" VTK file.\n");
    data->f->Delete();

    delete data; data=NULL;
    return NULL;
}

void VTK::save(float t)
{
    unsigned int i;

    // Check the fields to write
    std::vector<std::string> fields = simData().sets.at(setId())->outputFields();
    if(!fields.size()){
        LOG(L_ERROR, "0 fields were set to be saved into the file.\n");
        throw std::runtime_error("No fields have been marked to be saved");
    }
    bool have_r = false;
    for(auto field : fields){
        if(!field.compare("r")){
            have_r = true;
            break;
        }
    }
    if(!have_r){
        LOG(L_ERROR, "\"r\" field was not set to be saved into the file.\n");
        throw std::runtime_error("\"r\" field is mandatory");
    }

    // Setup the data struct for the parallel thread
    data_pthread *data = new data_pthread;
    data->fields = fields;
    data->bounds = bounds();
    data->C = CalcServer::CalcServer::singleton();
    data->S = Logger::singleton();
    data->data = download(fields);
    if(!data->data.size()){
        LOG(L_ERROR, "\"r\" field was not set to be saved into the file.\n");
        throw std::runtime_error("Failure downloading data");
    }
    data->f = create();

    // Launch the thread
    pthread_t tid;
    int err;
    err = pthread_create(&tid, NULL, &save_pthread, (void*)data);
    if(err){
        LOG(L_ERROR, "Failure launching the parallel thread.\n");
        char err_str[strlen(strerror(err)) + 2];
        strcpy(err_str, strerror(err));
        strcat(err_str, "\n");
        LOG0(L_DEBUG, err_str);
        throw std::runtime_error("Failure launching VTK saving thread");
    }
    _tids.push_back(tid);

    // Clear the already finished threads
    if(_tids.size() > 0){
        i = _tids.size() - 1;
        while(true){
            if(pthread_kill(_tids.at(i), 0)){
                pthread_join(_tids.at(i), NULL);
                _tids.erase(_tids.begin() + i);
            }
            if(i == 0) break;
            i--;
        }
    }

    // Check and limit the number of active writing processes
    if(_tids.size() > 2){
        LOG(L_WARNING, "More than 2 active writing tasks\n");
        LOG(L_DEBUG, "This may result in heavy performance penalties, and hard disk failures\n");
        LOG(L_DEBUG, "Please, consider a reduction of the output printing rate\n");
        while(_tids.size() > 2){
            pthread_join(_tids.at(0), NULL);
            _tids.erase(_tids.begin());
        }
    }

    updatePVD(t);
}

void VTK::waitForSavers(){
    for(auto tid : _tids){
        pthread_join(tid, NULL);
    }
    _tids.clear();
}

const unsigned int VTK::compute_n()
{
    vtkSmartPointer<vtkXMLUnstructuredGridReader> f =
        vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();

    if(!f->CanReadFile(simData().sets.at(setId())->inputPath().c_str())){
        std::ostringstream msg;
        msg << "Cannot load VTK file \""
            <<  simData().sets.at(setId())->inputPath()
            << "\"!" << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Failure reading file");
    }

    f->SetFileName(simData().sets.at(setId())->inputPath().c_str());
    f->Update();

    vtkSmartPointer<vtkUnstructuredGrid> grid = f->GetOutput();

    return (const unsigned int)grid->GetNumberOfPoints();
}

vtkXMLUnstructuredGridWriter* VTK::create(){
    vtkXMLUnstructuredGridWriter *f = NULL;

    std::string basename = simData().sets.at(setId())->outputPath();
    // Check that {index} scape string is present, for backward compatibility
    if(basename.find("{index}") == std::string::npos){
        basename += ".{index}.vtu";
    }
    _next_file_index = file(basename, _next_file_index);

    std::ostringstream msg;
    msg << "Writing \"" << file() << "\" ASCII file..." << std::endl;
    LOG(L_INFO, msg.str());

    f = vtkXMLUnstructuredGridWriter::New();
    basename = file();
    f->SetFileName(basename.c_str());
    _next_file_index++;

    return f;
}

void VTK::updatePVD(float t){
    unsigned int n;

    std::ostringstream msg;
    msg << "Writing \"" << filenamePVD() << "\" Paraview data file..." << std::endl;
    LOG(L_INFO, msg.str());

    bool should_release_doc = false;
    DOMDocument* doc = getPVD(false);
    if(!doc){
        should_release_doc = true;
        doc = getPVD(true);
    }
    DOMElement* root = doc->getDocumentElement();
    if(!root){
        LOG(L_ERROR, "Empty XML file found!\n");
        throw std::runtime_error("Bad XML file format");
    }
    n = doc->getElementsByTagName(xmlS("VTKFile"))->getLength();
    if(n != 1){
        std::ostringstream msg;
        msg << "Expected 1 VTKFile root section, but " << n
            << "have been found" << std::endl;
        LOG(L_ERROR, msg.str());
        throw std::runtime_error("Bad XML file format");
    }

    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Collection"));
    if(nodes->getLength() != 1){
        std::ostringstream msg;
        msg << "Expected 1 collection, but " << nodes->getLength()
            << "have been found" << std::endl;
        LOG(L_ERROR, msg.str());
    }
    DOMNode* node = nodes->item(0);
    DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);

    DOMElement *s_elem;
    s_elem = doc->createElement(xmlS("DataSet"));
    s_elem->setAttribute(xmlS("timestep"), xmlS(std::to_string(t)));
    s_elem->setAttribute(xmlS("group"), xmlS(""));
    s_elem->setAttribute(xmlS("part"), xmlS("0"));
    s_elem->setAttribute(xmlS("file"), xmlS(file()));
    elem->appendChild(s_elem);

    // Save the XML document to a file
    DOMImplementation* impl;
    DOMLSSerializer* saver;
    impl = DOMImplementationRegistry::getDOMImplementation(xmlS("LS"));
    saver = ((DOMImplementationLS*)impl)->createLSSerializer();

    if(saver->getDomConfig()->canSetParameter(XMLUni::fgDOMWRTFormatPrettyPrint, true))
        saver->getDomConfig()->setParameter(XMLUni::fgDOMWRTFormatPrettyPrint, true);
    saver->setNewLine(xmlS("\r\n"));

    std::string fname = filenamePVD();
    XMLFormatTarget *target = new LocalFileFormatTarget(fname.c_str());
    // XMLFormatTarget *target = new StdOutFormatTarget();
    DOMLSOutput *output = ((DOMImplementationLS*)impl)->createLSOutput();
    output->setByteStream(target);
    output->setEncoding(xmlS("UTF-8"));

    try {
        saver->write(doc, output);
    }
    catch( XMLException& e ){
        std::string message = xmlS(e.getMessage());
        LOG(L_ERROR, "XML toolkit writing error.\n");
        std::ostringstream msg;
        msg << "\t" << message << std::endl;
        LOG0(L_DEBUG, msg.str());
        xmlClear();
        throw;
    }
    catch( DOMException& e ){
        std::string message = xmlS(e.getMessage());
        LOG(L_ERROR, "XML DOM writing error.\n");
        std::ostringstream msg;
        msg << "\t" << message << std::endl;
        LOG0(L_DEBUG, msg.str());
        xmlClear();
        throw;
    }
    catch( ... ){
        LOG(L_ERROR, "Writing error.\n");
        LOG0(L_DEBUG, "\tUnhandled exception\n");
        xmlClear();
        throw;
    }

    target->flush();

    delete target;
    saver->release();
    output->release();
    if(should_release_doc)
        doc->release();
    xmlClear();
}

DOMDocument* VTK::getPVD(bool generate)
{
    DOMDocument* doc = NULL;
    FILE *dummy=NULL;

    // Try to open as ascii file, just to know if the file already exist
    dummy = fopen(filenamePVD().c_str(), "r");
    if(!dummy){
        if(!generate){
            return NULL;
        }
        DOMImplementation* impl;
        impl = DOMImplementationRegistry::getDOMImplementation(xmlS("Range"));
        DOMDocument* doc = impl->createDocument(
            NULL,
            xmlS("VTKFile"),
            NULL);
        DOMElement* root = doc->getDocumentElement();
        root->setAttribute(xmlS("type"), xmlS("Collection"));
        root->setAttribute(xmlS("version"), xmlS("0.1"));
        DOMElement *elem;
        elem = doc->createElement(xmlS("Collection"));
        root->appendChild(elem);
        return doc;
    }
    fclose(dummy);
    XercesDOMParser *parser = new XercesDOMParser();
    parser->setValidationScheme(XercesDOMParser::Val_Never);
    parser->setDoNamespaces(false);
    parser->setDoSchema(false);
    parser->setLoadExternalDTD(false);
    std::string fname = filenamePVD();
    parser->parse(fname.c_str());
    doc = parser->getDocument();
    parsers.push_back(parser);
    return doc;
}

const std::string VTK::filenamePVD()
{
    if(_namePVD == ""){
        try {
            unsigned int i=0;
            _namePVD = newFilePath(
                simData().sets.at(setId())->outputPath() + ".pvd", i, 1);
        } catch(std::invalid_argument e) {
            std::ostringstream msg;
            _namePVD = setStrConstantsCopy(
                simData().sets.at(setId())->outputPath()) + ".pvd";
            msg << "Overwriting '" << _namePVD << "'" << std::endl;
            LOG(L_WARNING, msg.str());
        }
    }
    return _namePVD;
}

}}  // namespace

#endif // HAVE_VTK
