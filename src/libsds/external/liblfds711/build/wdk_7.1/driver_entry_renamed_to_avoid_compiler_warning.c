#include "liblfds711_internal.h"





/****************************************************************************/
DRIVER_INITIALIZE DriverEntry;





/****************************************************************************/
#pragma warning( disable : 4100 )

NTSTATUS DriverEntry( struct _DRIVER_OBJECT *DriverObject, PUNICODE_STRING RegistryPath )
{
	return STATUS_SUCCESS;
}

#pragma warning( default : 4100 )

