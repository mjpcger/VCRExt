/*
* Copyright 2020 Martin Conrad
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/
#define DEFINEGLOBALS
#include "VCRExtMain.h"
/***************************
 * Change value of EXTENTRY to the resulting dll name + _Init,
 * all lowercase characters, except the first character,
 * e.g. for MyExt.dll to Myext_Init.
 **************************/
#define EXTENTRY Vcrext_Init
/**
 * Initialization of tcl stubs, dummy if not compiled for stubs support
 */
static int initTclStubs(Tcl_Interp *pi) {
#if USE_TCL_STUBS
	struct HeadOfInterp : public Tcl_Interp {
		TclStubs *stubTable;
	} *hoi = (HeadOfInterp*) pi;
	if (hoi->stubTable == NULL || hoi->stubTable->magic != TCL_STUB_MAGIC) {
		pi->result = "This extension requires Tcl stubs support.";
		pi->freeProc = TCL_STATIC;
		return 0;
	}
	tclStubsPtr = hoi->stubTable;
	if (Tcl_PkgRequire(pi, "Tcl", "8.5", 0) == NULL) {
		tclStubsPtr = NULL;
		return 0;
    }
    if (tclStubsPtr->hooks != NULL) {
		tclPlatStubsPtr = tclStubsPtr->hooks->tclPlatStubs;
		tclIntStubsPtr = tclStubsPtr->hooks->tclIntStubs;
		tclIntPlatStubsPtr = tclStubsPtr->hooks->tclIntPlatStubs;
    }
#endif
	return 1;
}

/**
 * Initialization of tk stubs, dummy if not compiled for stubs support
 */
static int initTkStubs(Tcl_Interp *pi) {
#if USE_TK_STUBS
    if (Tcl_PkgRequireEx(pi, "Tk", "8.5", 0, (ClientData*) &tkStubsPtr) == NULL)
		return 0;
    if (tkStubsPtr == NULL || tkStubsPtr->hooks == NULL) {
      Tcl_SetResult(pi, "This extension requires Tk stubs support.", TCL_STATIC);
      return 0;
    }
    tkPlatStubsPtr = tkStubsPtr->hooks->tkPlatStubs;
    tkIntStubsPtr = tkStubsPtr->hooks->tkIntStubs;
    tkIntPlatStubsPtr = tkStubsPtr->hooks->tkIntPlatStubs;
    tkIntXlibStubsPtr = tkStubsPtr->hooks->tkIntXlibStubs;
#endif
	return 1;
}

extern "C" DLLEXPORT int EXTENTRY(Tcl_Interp *pi) {
	NewCmdDesc *act;
	if (!initTclStubs(pi) || !initTkStubs(pi))
		return TCL_ERROR;
	for (act = NewCmdDesc::Head; act; act = act->Next)
		Tcl_CreateObjCommand(pi, act->Name, act->Entry, act->Data, act->Cleanup);
	return TCL_OK;
}

// Get name of object type

const char *getObjTypeName(Tcl_Obj*obj){
	if (obj->typePtr)
		return obj->typePtr->name;
	return "";
}

// Implementation of ParamList class

void ParamList::init(int count) {
	Size = count;
	for (Max = count = 0; count < Size; count++)
		Objects[count] = 0;
}
void ParamList::free() {
	while (Max > 0) {
		if (Objects[--Max]) {
			Tcl_DecrRefCount(Objects[Max]);
			Objects[Max] = NULL;
		}
	}
}
ParamList::ParamList(int count) {
	Objects = (Tcl_Obj**) Tcl_Alloc(count * sizeof *Objects);
	init(count);
}
ParamList::~ParamList() {
	free();
	Tcl_Free((char*)Objects);
}
ParamList& ParamList::operator[](int count) {
	// resize-Operator
	free();
	Objects = (Tcl_Obj**) Tcl_Realloc((char*)Objects, count * sizeof *Objects);
	init(count);
	return *this;
}
ParamList::operator Tcl_Obj**() {
	return Objects;
}
ParamList::operator int() {
	return Max;
}
ParamList& ParamList::operator,(Tcl_Obj* obj) {
	Objects[Max++] = obj;
	Tcl_IncrRefCount(obj);
	return *this;
}

// Implementation Value class

Tcl_ObjType *Value::getTypeObj() {
	return NULL;
}
const char *Value::getType() {
	return V->typePtr == NULL ? "" : V->typePtr->name;
}
Value::Value() {
	V = Tcl_NewObj();
	Tcl_IncrRefCount(V);
	Assigned = false;
}
Value::Value(Tcl_Obj*obj) {
	V = obj;
	Assigned = true;
}
Value::Value(Tcl_Obj&obj) {
	Tcl_DecrRefCount(V);
	V = Tcl_DuplicateObj(&obj);
	Tcl_IncrRefCount(V);
	Assigned = true;
}
Value::~Value() {
	if (!Assigned)
		Tcl_DecrRefCount(V);
	V = NULL;
}
Value::operator Tcl_Obj*() const {
	return V;
}

// Implementation Int class

Tcl_ObjType *Int::getTypeObj() {
	static Tcl_ObjType *myType = (Tcl_ObjType*)~0;

	if (myType == (Tcl_ObjType*)~0)
		myType = Tcl_GetObjType("int");
	return myType;
}
Int::Int(int v) {
	Tcl_SetIntObj(V, v);
	TypeChecked = true;
}
Int::Int(const Int&v) {
	Tcl_DecrRefCount(V);
	V = Tcl_DuplicateObj(v.V);
	Tcl_IncrRefCount(V);
	TypeChecked = v.TypeChecked;
}
Int::Int(Tcl_Obj&obj) {
	Tcl_SetIntObj(V, (int)Int(&obj));
	TypeChecked = true;
}
int Int::checkType() {
	int rc;
	if (!TypeChecked) {
		if (Tcl_GetIntFromObj(NULL, V, &rc) == TCL_ERROR) {
			double rc1 = 0;
			if (Tcl_GetDoubleFromObj(NULL, V, &rc1) == TCL_ERROR)
				throw ValueException(ValueException::TypeMismatch, "No numeric value");
			rc = (int) rc1;
		}
		Tcl_SetIntObj(V, rc);
		TypeChecked = true;
	}
	else if (Tcl_GetIntFromObj(NULL, V, &rc) == TCL_ERROR)
		// This shouldn't happen cause type has been checked previously
		throw ValueException(ValueException::TypeMismatch, "No numeric value");
	return rc;
}
Int::Int(Tcl_Obj *obj) {
	Tcl_DecrRefCount(V);
	V = obj;
	Assigned = true;
	TypeChecked = false;
}
Int::operator int() const {
	int rc;
	if (Tcl_GetIntFromObj(NULL, V, &rc) != TCL_OK) {
		double rc1;
		if (Tcl_GetDoubleFromObj(NULL, V, &rc1) == TCL_ERROR)
			throw ValueException(ValueException::TypeMismatch, "No numeric value");
		rc = (int) rc1;
	}
	return rc;
}
Int& Int::operator=(Int val) {
	Tcl_SetIntObj(V, val.checkType());
	return *this;
}
Int& Int::operator=(int val) {
	Tcl_SetIntObj(V, val);
	return *this;
}
bool Int::isInt(Tcl_Obj *p) {
	int val;
	return Tcl_GetIntFromObj(NULL, p, &val) == TCL_OK ? true : false;
}
Int Int::operator-() {
	int a = checkType();
	if (a && a == -a)
		throw ValueException(ValueException::Overflow, "Invalid negation");
	return Int(-a);
}
Int Int::operator~() {
	return Int(~checkType());
}
Int Int::operator--(int) {
	int a = checkType();
	if (a - 1 > 0 && a < 0)
		throw ValueException(ValueException::Overflow, "Decrement reached limit");
	Tcl_SetIntObj(V, a - 1);
	return Int(a);
}
Int& Int::operator--() {
	int a = checkType();
	if (a - 1 > 0 && a < 0)
		throw ValueException(ValueException::Overflow, "Decrement reached limit");
	Tcl_SetIntObj(V, a - 1);
	return *this;
}
Int Int::operator++(int) {
	int a = checkType();
	if (a + 1 < 0 && a > 0)
		throw ValueException(ValueException::Overflow, "Increment reached limit");
	Tcl_SetIntObj(V, a + 1);
	return Int(a);
}
Int& Int::operator++() {
	int a = checkType();
	if (a + 1 < 0 && a > 0)
		throw ValueException(ValueException::Overflow, "Increment reached limit");
	Tcl_SetIntObj(V, a + 1);
	return *this;
}
Int operator+(Int s1, Int s2) {
	int a1 = s1.checkType(), a2 = s2.checkType(), ssum;
	ssum = a1 + a2;
	if ((s1 < 0) + (s2 < 0) != 1 && (s1 < 0) + (ssum < 0) == 1)
		throw ValueException(ValueException::Overflow, "Sum reached limit");
	return Int(ssum);
}
Int operator*(Int s1, Int s2) {
	int a1 = s1.checkType(), a2 = s2.checkType(), ssum;
	ssum = a1 * a2;
	if (a1 && ssum / a1 != a2)
		throw ValueException(ValueException::DivideByZero, "Product overflow");
	return Int(ssum);
}
Int operator/(Int s1, Int s2) {
	int a1 = s1.checkType(), a2 = s2.checkType();
	if (a2 == 0)
		throw ValueException(ValueException::DivideByZero, "Divide by zero");
	return Int(a1 / a2);
}
Int operator%(Int s1, Int s2) {
	int a1 = s1.checkType(), a2 = s2.checkType();
	if (a2 == 0)
		throw ValueException(ValueException::DivideByZero, "Divide by zero");
	return Int(a1 % a2);
}

Tcl_ObjType *String::getTypeObj() {
	static Tcl_ObjType *myType = (Tcl_ObjType*)~0;

	if (myType == (Tcl_ObjType*)~0)
		myType = Tcl_GetObjType("string");
	return myType;
}
String::String(const char *v) {
	Tcl_SetStringObj(V, v, -1);
}
String::String(const String&v) {
	Tcl_SetStringObj(V, Tcl_GetString(v.V), -1);
}
String::String(Tcl_Obj*obj) : Value(obj) {
}
String::String(Tcl_Obj &obj) {
	Tcl_SetStringObj(V, Tcl_GetString(&obj), -1);
}
String::operator const char*() const {
	return Tcl_GetString(V);
}
String String::operator+(const String& arg) {
	String res(*this);
	Tcl_AppendObjToObj(res.V, arg.V);
	return res;
}
String& String::operator=(const String& val) {
	Tcl_SetStringObj(V, Tcl_GetString(val.V), -1);
	return *this;
}

Tcl_ObjType *PtrValue::getTypeObj() {
	static Tcl_ObjType *myType = (Tcl_ObjType*)~0;

	if (myType == (Tcl_ObjType*)~0)
		myType = Tcl_GetObjType("string");
	return myType;
}
PtrValue::PtrValue(Tcl_WideInt v) {
	Tcl_SetWideIntObj(V, v);
}
PtrValue::PtrValue(const PtrValue&v) {
	Tcl_DecrRefCount(V);
	V = Tcl_DuplicateObj(v.V);
	Tcl_IncrRefCount(V);
}
PtrValue::PtrValue(Tcl_Obj*obj) {
	Tcl_DecrRefCount(V);
	V = obj;
	Assigned = true;
}
PtrValue::PtrValue(Tcl_Obj &obj) {
	Tcl_SetWideIntObj(V, (Tcl_WideInt)PtrValue(&obj));
}
PtrValue::operator Tcl_WideInt() const {
	Tcl_WideInt rc;
	if (Tcl_GetWideIntFromObj(NULL, V, &rc) != TCL_OK) {
		throw ValueException(ValueException::TypeMismatch, "No valid value");
	}
	return rc;
}
PtrValue& PtrValue::operator=(PtrValue val) {
	Tcl_SetWideIntObj(V, (Tcl_WideInt)val);
	return *this;
}
PtrValue& PtrValue::operator=(Tcl_WideInt val) {
	Tcl_SetWideIntObj(V, val);
	return *this;
}
