TOPDIR = ..\..

TARGET = pkcs15init.lib

HEADERS = pkcs15-init.h keycache.h
HEADERSDIR = $(TOPDIR)\src\include\opensc

OBJECTS = profile.obj pkcs15-lib.obj keycache.obj \
          pkcs15-miocos.obj pkcs15-gpk.obj pkcs15-cflex.obj \
          pkcs15-etoken.obj pkcs15-jcop.obj pkcs15-starcos.obj \
	  pkcs15-oberthur.obj

all: install-headers $(TARGET)

$(TARGET): $(OBJECTS)
	lib /nologo /machine:ix86 /out:$(TARGET) $(OBJECTS)

!INCLUDE $(TOPDIR)\win32\Make.rules.mak

