int main(void)
{
    struct platform_device device = {0};
    struct arm_pmu pmu = {.active_irqs=1, .free_pmu_irq=multicore_free_irq,
        .save_pm_registers=save_registers, .restore_pm_registers=restore_registers,
        .reset=reset_registers, .plat_device=&device};
    cpu_pmu=&pmu;
    irq_enabled=true;
    assert(pmu_cpu_notify(NULL, CPU_DOWN_PREPARE, (void *)3)==NOTIFY_OK);
    assert(pmu_cpu_notify(NULL, CPU_DOWN_FAILED, (void *)3)==NOTIFY_OK);
    assert(irq_enabled && disables==0 && saves==0);
    for (unsigned cycle=0; cycle<20; cycle++) {
        unsigned frozen=cycle%2 ? CPU_TASKS_FROZEN : 0;
        assert(pmu_cpu_notify(NULL, CPU_DYING|frozen, (void *)3)==NOTIFY_OK);
        assert(!irq_enabled && disables==cycle+1 && saves==cycle+1);
        from_idle=1;
        assert(pmu_cpu_notify(NULL, CPU_STARTING|frozen, (void *)3)==NOTIFY_OK);
        assert(irq_enabled && enables==cycle+1 && resets==cycle+1 && restores==cycle+1);
        assert(from_idle==0);
    }
    pmu_cpu_notify(NULL, CPU_DYING, (void *)3);
    pmu.active_irqs=0;
    pmu_cpu_notify(NULL, CPU_STARTING, (void *)3);
    assert(!irq_enabled && enables==20);
    cpu_pmu=NULL;
    assert(pmu_cpu_notify(NULL, CPU_STARTING, (void *)3)==NOTIFY_DONE);
    puts("PASS: 20 PPI lifecycles, frozen hotplug, down veto, empty-slot IRQ ownership, freed IRQ startup");
    return 0;
}
