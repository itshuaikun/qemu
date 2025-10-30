#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "qemu/typedefs.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/numa.h"
#include "hw/riscv/boot.h"
#include "hw/sysbus.h"
#include "system/address-spaces.h"
#include "hw/loader.h"

#define RHEA_RISCV_CPUS_MAX 4
#define RHEA_SOCKETS_MAX 1
#define TYPE_RHEA_MACHINE MACHINE_TYPE_NAME("rhea")
typedef struct RheaMachineState RheaMachineState;
DECLARE_INSTANCE_CHECKER(RheaMachineState, RHEA_MACHINE,
                         TYPE_RHEA_MACHINE)

typedef struct RheaMachineState {
    MachineState parent_obj;
    RISCVHartArrayState soc[RHEA_SOCKETS_MAX];
} RheaMachineState;

enum {
    RHEA_ROM = 0,
    RHEA_SRAM,
    RHEA_UART0,
    RHEA_DRAM,
};
static const MemMapEntry rhea_memmap[] = {
    [RHEA_ROM] = { 0x00000000, 256 * KiB },
    [RHEA_SRAM] = { 0x00100000, 512 * KiB },
    [RHEA_UART0] = { 0x06000000, 0x100 },
    [RHEA_DRAM] = { 0x40000000, 570 * MiB },
};

static void rhea_machine_init(MachineState *ms)
{
    RheaMachineState *s = RHEA_MACHINE(ms);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    char *soc_name;
    int i, base_hartid, hart_count;

    /* Check socket count limit */
    if (RHEA_SOCKETS_MAX < riscv_socket_count(ms)) {
        error_report("number of sockets/nodes should be less than %d",
            RHEA_SOCKETS_MAX);
        exit(1);
    }

    /* Initialize sockets */
    for (i = 0; i < riscv_socket_count(ms); i++) {
        if (!riscv_socket_check_hartids(ms, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(ms, i);
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }

        hart_count = riscv_socket_hart_count(ms, i);
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }

        soc_name = g_strdup_printf("soc%d", i);
        object_initialize_child(OBJECT(ms), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        g_free(soc_name);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                ms->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "resetvec",
                                rhea_memmap[RHEA_ROM].base, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);
    }

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, rhea_memmap[RHEA_DRAM].base,
                                ms->ram);
    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.rhea.mrom",
                           rhea_memmap[RHEA_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, rhea_memmap[RHEA_ROM].base,
                                mask_rom);
    /* reset vector */
    uint32_t reset_vec[4] = {
        0x0000006f // j .
    };
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          rhea_memmap[RHEA_ROM].base, &address_space_memory);
    /* register SRAM */
    memory_region_init_ram(sram, NULL, "riscv.rhea.sram",
                           rhea_memmap[RHEA_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory, rhea_memmap[RHEA_SRAM].base,
                                sram);
}

static void rhea_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);

    mc->desc = "RISC-V Rhea Machine";
    mc->init = rhea_machine_init;
    mc->max_cpus = RHEA_RISCV_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv.rhea.dram";
    mc->default_ram_size = rhea_memmap[RHEA_DRAM].size;
}

static void rhea_machine_instance_init(Object *obj)
{

}

static const TypeInfo rhea_machine_typeinfo = {
        .name = MACHINE_TYPE_NAME("rhea"),
        .parent = TYPE_MACHINE,
        .class_init = rhea_machine_class_init,
        .instance_init = rhea_machine_instance_init,
        .instance_size = sizeof(RheaMachineState),
};

static void rhea_machine_init_register_types(void)
{
    type_register_static(&rhea_machine_typeinfo);
}

type_init(rhea_machine_init_register_types)