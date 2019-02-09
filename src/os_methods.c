
#include <lai.h>

static const char *acpi_emulated_os = "Microsoft Windows NT";        // OS family
static uint64_t acpi_implemented_version = 2;                // ACPI 2.0

static const char *supported_osi_strings[] =
{
    "Windows 2000",        /* Windows 2000 */
    "Windows 2001",        /* Windows XP */
    "Windows 2001 SP1",    /* Windows XP SP1 */
    "Windows 2001.1",    /* Windows Server 2003 */
    "Windows 2006",        /* Windows Vista */
    "Windows 2006.1",    /* Windows Server 2008 */
    "Windows 2006 SP1",    /* Windows Vista SP1 */
    "Windows 2006 SP2",    /* Windows Vista SP2 */
    "Windows 2009",        /* Windows 7 */
    "Windows 2012",        /* Windows 8 */
    "Windows 2013",        /* Windows 8.1 */
    "Windows 2015",        /* Windows 10 */
};

// When executing the _OSI() method, we'll have one parameter which contains
// the name of an OS. We have to pretend to be a modern version of Windows,
// for AML to let us use its features.
int acpi_do_osi_method(acpi_object_t *args, acpi_object_t *result)
{
    uint32_t osi_return = 0;
    for(int i = 0; i < (sizeof(supported_osi_strings) / sizeof(uintptr_t)); i++)
    {
        if(!acpi_strcmp(args[0].string, supported_osi_strings[i]))
        {
            osi_return = 0xFFFFFFFF;
            break;
        }
    }

    if(!osi_return && !acpi_strcmp(args[0].string, "Linux"))
        acpi_warn("buggy BIOS requested _OSI('Linux'), ignoring...\n");

    result->type = ACPI_INTEGER;
    result->integer = osi_return;

    acpi_debug("_OSI('%s') returned 0x%08X\n", args[0].string, osi_return);
    return 0;
}

// OS family -- pretend to be Windows
int acpi_do_os_method(acpi_object_t *args, acpi_object_t *result)
{
    result->type = ACPI_STRING;
    result->string = acpi_malloc(acpi_strlen(acpi_emulated_os));
    acpi_strcpy(result->string, acpi_emulated_os);

    acpi_debug("_OS_ returned '%s'\n", result->string);
    return 0;
}

// All versions of Windows starting from Windows Vista claim to implement
// at least ACPI 2.0. Therefore we also need to do the same.
int acpi_do_rev_method(acpi_object_t *args, acpi_object_t *result)
{
    result->type = ACPI_INTEGER;
    result->integer = acpi_implemented_version;

    acpi_debug("_REV returned %d\n", result->integer);
    return 0;
}

