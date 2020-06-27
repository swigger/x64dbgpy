MACROS=-DUNICODE -D_UNICODE

all:AAEnv.dll

AAEnv.dll:AAEnv.obj
	link /nologo  /dll /out:$@ $^

AAEnv.obj:AAEnv.cpp
	cl /nologo /c /EHsc /O2 $(MACROS) $^

install: AAEnv.dll
	copy AAEnv.dll ..\release\x32\plugins\AAEnv.dp32
