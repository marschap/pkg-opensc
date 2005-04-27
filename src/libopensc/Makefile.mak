TOPDIR = ..\..


TARGET                  = opensc.dll

HEADERS			= \
	opensc.h pkcs15.h emv.h \
	cardctl.h asn1.h log.h ui.h \
	errors.h types.h

HEADERSDIR		= $(TOPDIR)\src\include\opensc

OBJECTS			= \
	sc.obj ctx.obj ui.obj log.obj errors.obj portability.obj module.obj \
	asn1.obj base64.obj sec.obj card.obj iso7816.obj dir.obj padding.obj \
	\
	pkcs15.obj pkcs15-cert.obj pkcs15-data.obj pkcs15-pin.obj \
	pkcs15-prkey.obj pkcs15-pubkey.obj pkcs15-sec.obj \
	pkcs15-wrap.obj pkcs15-algo.obj pkcs15-cache.obj pkcs15-syn.obj \
	\
	emv.obj \
	\
	ctbcs.obj reader-ctapi.obj reader-pcsc.obj \
	pinpad-ccid.obj \
	\
	card-setcos.obj card-miocos.obj card-flex.obj card-gpk.obj \
	card-etoken.obj card-tcos.obj card-emv.obj card-default.obj \
	card-mcrd.obj card-starcos.obj card-openpgp.obj card-jcop.obj \
	card-oberthur.obj \
	\
	pkcs15-openpgp.obj pkcs15-infocamere.obj pkcs15-starcert.obj \
	pkcs15-netkey.obj pkcs15-esteid.obj \
	\
	$(TOPDIR)\win32\version.res

all: install-headers $(TARGET)

!INCLUDE $(TOPDIR)\win32\Make.rules.mak

$(TARGET): $(OBJECTS) ..\scconf\scconf.lib ..\scdl\scdl.lib ..\common\common.lib
	perl $(TOPDIR)\win32\makedef.pl $*.def $* $(OBJECTS)
	link $(LINKFLAGS) /dll /def:$*.def /implib:$*.lib /out:$(TARGET) $(OBJECTS) ..\scconf\scconf.lib ..\scdl\scdl.lib ..\common\common.lib winscard.lib
