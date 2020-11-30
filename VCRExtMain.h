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
#ifndef VCREXTMAIN_H
# define VCREXTMAIN_H
# include "tcl.h"
# include "tclDecls.h"
# include "tk.h"
# include "tkDecls.h"

# if USE_TCL_STUBS
#  ifdef DEFINEGLOBALS
	TclStubs *tclStubsPtr;
	TclPlatStubs *tclPlatStubsPtr;
	struct TclIntStubs *tclIntStubsPtr;
	struct TclIntPlatStubs *tclIntPlatStubsPtr;
#  else
	extern TclStubs *tclStubsPtr;
	extern TclPlatStubs *tclPlatStubsPtr;
	extern struct TclIntStubs *tclIntStubsPtr;
	extern struct TclIntPlatStubs *tclIntPlatStubsPtr;
#  endif
# endif
# if USE_TK_STUBS
#  ifdef DEFINEGLOBALS
	TkStubs *tkStubsPtr;
	struct TkPlatStubs *tkPlatStubsPtr;
	struct TkIntStubs *tkIntStubsPtr;
	struct TkIntPlatStubs *tkIntPlatStubsPtr;
	struct TkIntXlibStubs *tkIntXlibStubsPtr;
#  else
	extern TkStubs *tkStubsPtr;
	extern struct TkPlatStubs *tkPlatStubsPtr;
	extern struct TkIntStubs *tkIntStubsPtr;
	extern struct TkIntPlatStubs *tkIntPlatStubsPtr;
	extern struct TkIntXlibStubs *tkIntXlibStubsPtr;
#  endif
# endif
	// Helper struct for EXTENTRY function. For each function, one such element must be defined
	// before EXTENTRY will be called. 
	struct NewCmdDesc {
		NewCmdDesc(const char *name, Tcl_ObjCmdProc *entry, ClientData data, Tcl_CmdDeleteProc *cleanup) {
			Name = name;
			Entry = entry;
			Data = data;
			Cleanup = cleanup;
			Next = Head;
			Head = this;
		}
		NewCmdDesc *Next;
		static NewCmdDesc *Head;
		const char *Name;
		Tcl_ObjCmdProc *Entry;
		Tcl_CmdDeleteProc *Cleanup;
		ClientData Data;
	};
	/*
	 * Class ParamList can be used to specify a parameter list for a call to an extension function.
	 * The constructor reserves space for the given no. of Tcl_Obj values. With the comma operator,
	 * the list can be filled. The Tcl_Obj** operator returns the Tcl_Obj pointer array.
	 * For example, a call to a tcl function with 3 agruments, can be done as follows:
	 * rc = function(data, interp, 3, (ParamList(3), arg1, arg2, arg3)); or (with macro PARA):
	 * rc = function(data, interp, PARA(3,(arg1, arg2, arg3)));
	 */
#	define PARA(n,args) n, (ParamList(n), args)
	class ParamList {
		int Size, Max;
		Tcl_Obj**Objects;
		void init(int count);
		void free();
	public:
		ParamList(int count);
		~ParamList();
		ParamList& operator[](int count);
		operator Tcl_Obj**();
		operator int();
		ParamList& operator,(Tcl_Obj* obj);
	};

	/*
	 * Base class for Value class exceptions. Contains an error description and an
	 * error code
	 */
	class ValueException {
	public:
		enum {TypeMismatch = 1, Overflow = 2, DivideByZero = 3, ValueExceptionLimit = 100};
		int ErrorCode;
		const char *ErrorDescription;
		ValueException(int ec = 0, const char*desc = NULL) {
			ErrorCode = ec, ErrorDescription = desc;
		}
	};
	/*
	 * Base class for all value classes derived from Tcl_Obj*. The Tcl_Obj* operator
	 * ensures that all classes derived from Value can be used to pass arguments via
	 * ParamList to tcl functions.
	 */
	class Value {
	protected:
		Tcl_Obj *V;
		bool Assigned;
		virtual Tcl_ObjType *getTypeObj();
	public:
		Value();
		~Value();
		Value(Tcl_Obj*);
		Value(Tcl_Obj&);
		operator Tcl_Obj*() const;
		const char *getType();
	};

	/*
	 * Class Int implements an integer value class based on Tcl_Obj. In this implementation, such
	 * values will be represented by 32-bit integer values (type int).
	 */
	class Int : public Value {
	protected:
		Tcl_ObjType *getTypeObj();
		bool TypeChecked;
		int checkType();			// Checks type, returns int value or throws TypeMismatch excetpion
	public:
		Int(int v);
		Int(const Int&v);
		Int(Tcl_Obj*obj);			// By reference, no object copy
		Int(Tcl_Obj &obj);			// By value, creates object copy, throws TypeMismatch exception
		operator int() const;		// Throws TypeMismatch exception if value wasn't numeric
		Int& operator=(Int val);	// Throws TypeMismatch exception if val isn't numeric
		Int& operator=(int);
		Int operator-();
		Int operator~();
		Int operator--(int);
		Int operator++(int);
		Int&operator--();			// prefix operands
		Int&operator++();
		bool isInt(Tcl_Obj *p);
		friend Int operator+(Int,Int);
		friend Int operator*(Int,Int);
		friend Int operator/(Int,Int);
		friend Int operator%(Int,Int);
	};
	Int operator+(Int,Int);			// Throws TypeMismatch exception if one value isn't numeric,
									// throws Overflow exception if sum breaks upper or lower boundary
	inline Int operator-(Int a,Int b) {
		return a + -b;
	}
	Int operator*(Int,Int);			// Throws TypeMismatch exception if one value isn't numeric,
									// throws Overflow exception if product breaks upper or lower boundary
	Int operator/(Int,Int);			// Throws TypeMismatch exception if one value isn't numeric,
									// throws DivideByZero exception
	Int operator%(Int,Int);			// Throws TypeMismatch exception if one value isn't numeric,
									// throws DivideByZero exception

	/*
	 * Class String implements a string value class based on Tcl_Obj. Such strings will be represented
	 * by UTF-8 encoded character arrays.
	 */
	class String : public Value {
	protected:
		Tcl_ObjType *getTypeObj();
	public:
		String(const char *v);
		String(const String&v);
		String(Tcl_Obj*obj);	// By reference, no object copy
		String(Tcl_Obj &obj);	// By value, creates object copy
		operator const char*() const;
		String operator+(const String& arg);
		String& operator=(const String& val);
	};

	/*
	 * Class PtrValue implements a class that represents pointers or handles stored in a Tcl_Obj.
	 * In this implementation, such pointers will be represented by 64-bit integer values (type long long).
	 */
	class PtrValue : public Value {
	protected:
		Tcl_ObjType * getTypeObj();
	public:
		PtrValue(Tcl_WideInt);
		PtrValue(const PtrValue&);
		PtrValue(Tcl_Obj*);
		PtrValue(Tcl_Obj&);
		operator Tcl_WideInt() const;
		PtrValue& operator=(PtrValue val);	// Throws TypeMismatch exception if val isn't numeric
		PtrValue& operator=(Tcl_WideInt);
	};
	/*
	 * Macro to declare a tcl command. Defines Tcl_ObjCmdProc function stub with arguments named
	 * cd, ip, cnt and objs. Implements 1st part of try..catch block.
	 *	Parameter description:
	 *	name	Name of function
	 *	n		no. of parameters. -1 for variable no. of arguments
	 *	d		Usage-String for error text in case of wrong no. of parameters
	 */
#	define DECLARE(name,n,d) static int name(ClientData cd, Tcl_Interp *ip, int cnt, Tcl_Obj*CONST objs[]) {\
		if (n >= 0 && n + 1 != cnt) {\
			Tcl_WrongNumArgs(ip,1,objs,d);\
			return TCL_ERROR;\
		}\
		try
	/*
	 * 2nd part of tcl command function stub. Implements the catch block and returns TCL_OK if no
	 * error occurs.
	 */
#	define FINISH catch (ValueException &e) {\
			Tcl_SetChannelErrorInterp(ip, String(e.ErrorDescription));\
			return TCL_ERROR;\
		}\
		return TCL_OK;\
	}
	/*
	 * Macro to declare Value type variables for command arguments in tcl procedures.
	 *	Parameters:
	 *	type	Object type name, e.g Int
	 *	name	Name of local variable
	 *	count	Argument no, starting at 1 for 1st argument
	 */
#	define ARG(type,name,count) type name(*objs[count]);
	/*
	 * Macros to declare return value for tcl command procedure.
	 *	Parameters:
	 *	type	Object type name, e.g. Int
	 *	name	Name of variable holding the return value
	 */
#	define RES(type,name) type name(Tcl_GetObjResult(ip));

const char *getObjTypeName(Tcl_Obj*obj);

# ifdef DEFINEGLOBALS
	NewCmdDesc *NewCmdDesc::Head = NULL;
# endif
#endif