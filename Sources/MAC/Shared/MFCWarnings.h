#pragma once

/**************************************************************************************************
Warnings
**************************************************************************************************/
#ifdef _MSC_VER
#pragma warning(disable: 4266) // comes from MFC
#pragma warning(disable: 4365) // comes from MFC
#pragma warning(disable: 4625) // copy constructor isn't there, but who cares
#pragma warning(disable: 4626) // about assigment operator, but not important
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4755) // branch not inlined, but who cares
#pragma warning(disable: 5026) // informational also about move operations
#pragma warning(disable: 5027) // informational about a move operator, but who cares
#pragma warning(disable: 5204) // MFC causes this
#pragma warning(disable: 5220) // MFC causes this
#pragma warning(disable: 5246) // initialization should be wrapped in braces (ATLComTime.h, etc.)
#endif
