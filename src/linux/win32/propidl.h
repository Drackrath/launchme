#pragma once
// Linux only: the values WIC's encoder options travel in, VARIANT (oaidl.h on Windows) and
// PROPBAG2 with IPropertyBag2 (ocidl.h), with the Windows SDK's names, values and signatures.
// Only what the launcher calls is declared; the vtables are this port's own, not Windows'.
#include "objidl.h"

using VARTYPE = unsigned short;
using VARIANT_BOOL = short;
using CLIPFORMAT = WORD;
enum VARENUM : VARTYPE {
    VT_EMPTY = 0, VT_NULL = 1, VT_I2 = 2, VT_I4 = 3, VT_R4 = 4, VT_R8 = 5, VT_BOOL = 11,
    VT_I1 = 16, VT_UI1 = 17, VT_UI2 = 18, VT_UI4 = 19, VT_I8 = 20, VT_UI8 = 21, VT_INT = 22, VT_UINT = 23,
};

// The scalar members only; `vt` tells which one holds the value.
struct VARIANT {
    VARTYPE vt; WORD wReserved1, wReserved2, wReserved3;
    union {
        LONGLONG llVal; LONG lVal; BYTE bVal; SHORT iVal; FLOAT fltVal; double dblVal; VARIANT_BOOL boolVal;
        CHAR cVal; USHORT uiVal; ULONG ulVal; ULONGLONG ullVal; INT intVal; UINT uintVal;
    };
};
inline void VariantInit(VARIANT* value) { value->vt = VT_EMPTY; }

struct PROPBAG2 { DWORD dwType; VARTYPE vt; CLIPFORMAT cfType; DWORD dwHint; LPOLESTR pstrName; CLSID clsid; };

struct IPropertyBag2 : public IUnknown {
    LM_IID(0x22f55882, 0x280b, 0x11d0, 0xa8, 0xa9, 0x00, 0xa0, 0xc9, 0x0c, 0x20, 0x04);
    using Base = IUnknown;
    // Each property by its pstrName.
    virtual HRESULT STDMETHODCALLTYPE Write(ULONG count, PROPBAG2* properties, VARIANT* values) = 0;
};
