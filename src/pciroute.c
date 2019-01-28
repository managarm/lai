
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018 by Omar Mohammad
 */

/* PCI IRQ Routing */
/* Every PCI device that is capable of generating an IRQ has an "interrupt pin"
   field in its configuration space. Contrary to what most people believe, this
   field is valid for both the PIC and the I/O APIC. The PCI local bus spec clearly
   says the "interrupt line" field everyone trusts are simply for BIOS or OS-
   -specific use. Therefore, nobody should assume it contains the real IRQ. Instead,
   the four PCI pins should be used: LNKA, LNKB, LNKC and LNKD. */

#include "lai.h"

#define PCI_PNP_ID		"PNP0A03"

// acpi_pci_route(): Resolves PCI IRQ routing for a specific device
// Param:	acpi_resource_t *dest - destination buffer
// Param:	uint8_t bus - PCI bus
// Param:	uint8_t slot - PCI slot
// Param:	uint8_t function - PCI function
// Return:	int - 0 on success

int acpi_pci_route(acpi_resource_t *dest, uint8_t bus, uint8_t slot, uint8_t function)
{
	//acpi_printf("acpi: attempt to resolve PCI IRQ for device %xb:%xb:%xb\n", bus, slot, function);

	// determine the interrupt pin
	uint8_t pin = (uint8_t)(acpi_pci_read(bus, slot, function, 0x3C) >> 8);
	if(pin == 0 || pin > 4)
		return 1;

	pin--;		// because PCI numbers the pins from 1, but ACPI numbers them from 0

	// find the PCI bus in the namespace
	acpi_object_t bus_number;
	acpi_object_t pnp_id;

	acpi_eisaid(&pnp_id, PCI_PNP_ID);

	size_t index = 0;
	acpi_handle_t *handle = acpins_get_deviceid(index, &pnp_id);
	char path[ACPI_MAX_NAME];
	int status;

	while(handle != NULL)
	{
		acpi_strcpy(path, handle->path);
		acpi_strcpy(path + acpi_strlen(path), "._BBN");	// _BBN: Base bus number

		status = acpi_eval(&bus_number, path);
		if(status != 0)
		{
			// when _BBN is not present, we assume bus 0
			bus_number.type = ACPI_INTEGER;
			bus_number.integer = 0;
		}

		if((uint8_t)bus_number.integer == bus)
			break;

		index++;
		handle = acpins_get_deviceid(index, &pnp_id);
	}

	if(handle == NULL)
		return 1;

	// read the PCI routing table
	acpi_strcpy(path, handle->path);
	acpi_strcpy(path + acpi_strlen(path), "._PRT");	// _PRT: PCI Routing Table

	acpi_object_t prt, prt_package, prt_entry;

	/* _PRT is a package of packages. Each package within the PRT is in the following format:
	   0: Integer:	Address of device. Low WORD = function, high WORD = slot
	   1: Integer:	Interrupt pin. 0 = LNKA, 1 = LNKB, 2 = LNKC, 3 = LNKD
	   2: Name or Integer:	If name, this is the namespace device which allocates the interrupt.
		If it's an integer, then this field is ignored.
	   3: Integer:	If offset 2 is a Name, this is the index within the resource descriptor
		of the specified device which contains the PCI interrupt. If offset 2 is an
		integer, this field is the ACPI GSI of this PCI IRQ. */

	status = acpi_eval(&prt, path);

	if(status != 0)
		return 1;

	size_t i = 0;

	while(1)
	{
		// read the _PRT package
		status = acpi_eval_package(&prt, i, &prt_package);
		if(status != 0)
			return 1;

		if(prt_package.type != ACPI_PACKAGE)
			return 1;

		// read the device address
		status = acpi_eval_package(&prt_package, 0, &prt_entry);
		if(status != 0)
			return 1;

		if(prt_entry.type != ACPI_INTEGER)
			return 1;

		// is this the device we want?
		if((prt_entry.integer >> 16) == slot)
		{
			if((prt_entry.integer & 0xFFFF) == 0xFFFF || (prt_entry.integer & 0xFFFF) == function)
			{
				// is this the interrupt pin we want?
				status = acpi_eval_package(&prt_package, 1, &prt_entry);
				if(status != 0)
					return 1;

				if(prt_entry.type != ACPI_INTEGER)
					return 1;

				if(prt_entry.integer == pin)
					goto resolve_pin;
			}
		}

		// nope, go on
		i++;
	}

resolve_pin:
	// here we've found what we need
	// is it a link device or a GSI?
	status = acpi_eval_package(&prt_package, 2, &prt_entry);
	if(status != 0)
		return 1;

	acpi_handle_t *link;		// PCI link device
	acpi_resource_t *res;
	size_t res_count;

	if(prt_entry.type == ACPI_INTEGER)
	{
		// GSI
		status = acpi_eval_package(&prt_package, 3, &prt_entry);
		if(status != 0)
			return 1;

		dest->type = ACPI_RESOURCE_IRQ;
		dest->base = prt_entry.integer;
		dest->irq_flags = ACPI_IRQ_LEVEL | ACPI_IRQ_ACTIVE_HIGH | ACPI_IRQ_SHARED;

		acpi_printf("acpi: PCI device %xb:%xb:%xb is using IRQ %d\n", bus, slot, function, (int)dest->base);
		return 0;
	} else if(prt_entry.type == ACPI_NAME)
	{
		// PCI Interrupt Link Device
		link = acpi_exec_resolve(prt_entry.name);
		if(!link)
			return 1;

		acpi_printf("acpi: PCI interrupt link is %s\n", link->path);

		// read the resource template of the device
		res = acpi_calloc(sizeof(acpi_resource_t), ACPI_MAX_RESOURCES);
		res_count = acpi_read_resource(link, res);

		if(!res_count)
			return 1;

		i = 0;
		while(i < res_count)
		{
			if(res[i].type == ACPI_RESOURCE_IRQ)
			{
				dest->type = ACPI_RESOURCE_IRQ;
				dest->base = res[i].base;
				dest->irq_flags = res[i].irq_flags;

				acpi_free(res);

				acpi_printf("acpi: PCI device %xb:%xb:%xb is using IRQ %d\n", bus, slot, function, (int)dest->base);
				return 0;
			}

			i++;
		}

		return 0;
	} else
		return 1;
}












