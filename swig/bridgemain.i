%module bridgemain
%{
#include "bridgemain.h"
#define nullptr 0
%}

// Allow Python Buffers
%include <pybuffer.i>
%include cpointer.i
%pointer_functions(int, intp);
%pointer_functions(__int64, i64p);
%pointer_functions(unsigned __int64, ui64p);
%pointer_functions(bool, boolp);


// Type Maps
%pybuffer_string(char* text);

%typemap(out) HWND {
    $result = PyInt_FromLong((long)$1);
}

%include "std_vector.i"

%template(ThreadVector) std::vector<THREADALLINFO>;

%inline %{
std::vector<THREADALLINFO> GetThreadInfoList(THREADLIST* l)
{
    std::vector<THREADALLINFO> vec(
        l->list,
        l->list + l->count
    );
    BridgeFree(l->list);
    return vec;
}
%}

%template(BpVector) std::vector<BRIDGEBP>;

%inline %{
std::vector<BRIDGEBP> GetBpList(BPMAP* bm)
{
    std::vector<BRIDGEBP> vec(
        bm->bp,
        bm->bp + bm->count
    );
    BridgeFree(bm->bp);
    return vec;
}
%}

%include <windows.i>
#define DECLSPEC_ALIGN(x) __declspec(align(x))
%include "..\pluginsdk\bridgemain.h"
