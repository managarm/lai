/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* LAI Embedded Controller implementation
 * ACPI 6.3 Specification chapter 12
 * ACPI defines an Embedded Controller for interfacing directly with firmware
 */

#include <lai/drivers/ec.h>

#define ACPI_EC_PNP_ID "PNP0C09"

void lai_init_ec(lai_nsnode_t *node, struct lai_ec_driver *driver){
    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state);

    LAI_CLEANUP_VAR lai_variable_t pnp_id = LAI_VAR_INITIALIZER;
    lai_eisaid(&pnp_id, ACPI_EC_PNP_ID);

    if(lai_check_device_pnp_id(node, &pnp_id, &state)){
        lai_warn("node supplied to lai_init_ec() is not an Embedded Controller");
        return;
    }

    // Found an EC
    lai_nsnode_t *crs_node = lai_resolve_path(node, "_CRS");
    if(!crs_node){
        lai_warn("Couldn't find _CRS for initializing EC driver");
        return;
    }

    LAI_CLEANUP_VAR lai_variable_t crs = LAI_VAR_INITIALIZER;
    if(lai_eval(&crs, crs_node, &state)){
        lai_warn("Couldn't eval _CRS for initializing EC driver");
        return;
    }
        
    struct lai_resource_view crs_it = LAI_RESOURCE_VIEW_INITIALIZER(&crs);
    lai_api_error_t error;

    error = lai_resource_iterate(&crs_it);
    if(error != LAI_ERROR_NONE){
        lai_warn("Encountered error while iterating EC _CRS: %s", lai_api_error_to_string(error));
        return;
    }
    enum lai_resource_type type = lai_resource_get_type(&crs_it);
    if(type != LAI_RESOURCE_IO){
        lai_warn("Unknown resource type while iterating EC _CRS: %02X", type);
        return;
    }
    driver->cmd_port = crs_it.base;

    error = lai_resource_iterate(&crs_it);
    if(error == LAI_ERROR_END_REACHED){
        // TODO: Support Hardware reduced ACPI systems
        return;
    } else if(error != LAI_ERROR_NONE){
        lai_warn("Encountered error while iterating EC _CRS: %s", lai_api_error_to_string(error));
        return;
    }
    type = lai_resource_get_type(&crs_it);
    if(type != LAI_RESOURCE_IO){
        lai_warn("Unknown resource type while iterating EC _CRS: %02X", type);
        return;
    }
    driver->data_port = crs_it.base;
}

uint8_t lai_read_ec(uint8_t offset, struct lai_ec_driver *driver){
    if(driver->cmd_port == 0 || driver->data_port == 0){
        lai_warn("EC driver has not yet been initialized");
        return;
    }

    if(!laihost_outb || !laihost_inb)
        lai_panic("host does not provide io functions required by lai_read_ec()");

    while(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_IBF))
        ;
    laihost_outb(driver->cmd_port, ACPI_EC_READ);
    laihost_outb(driver->data_port, offset);
    while(!(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_OBF)))
        ;
    return laihost_inb(driver->data_port);
}

void lai_write_ec(uint8_t offset, uint8_t value, struct lai_ec_driver *driver){
    if(driver->cmd_port == 0 || driver->data_port == 0){
        lai_warn("EC driver has not yet been initialized");
        return;
    }

    if(!laihost_outb)
        lai_panic("host does not provide io functions required by lai_read_ec()");

    while(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_IBF))
        ;
    laihost_outb(driver->cmd_port, ACPI_EC_WRITE);
    while(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_IBF))
        ;
    laihost_outb(driver->data_port, offset);
    while(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_IBF))
        ;
    laihost_outb(driver->data_port, value);
}

uint8_t lai_query_ec(struct lai_ec_driver *driver){
    if(driver->cmd_port == 0 || driver->data_port == 0){
        lai_warn("EC driver has not yet been initialized");
        return;
    }

    if(!laihost_outb || !laihost_inb)
        lai_panic("host does not provide io functions required by lai_read_ec()");

    laihost_outb(driver->cmd_port, ACPI_EC_QUERY); // Spec specifies that no interrupt will be generated for this command
    while(!(laihost_inb(driver->cmd_port) & (1 << ACPI_EC_STATUS_OBF)))
        ;
    return laihost_inb(driver->data_port);
}