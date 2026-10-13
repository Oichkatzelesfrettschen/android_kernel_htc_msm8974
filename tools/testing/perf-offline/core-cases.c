static void initialize(struct pmu *owner, struct pmu *alias)
{
    memset(contexts, 0, sizeof(contexts));
    *owner = (struct pmu){contexts, 1};
    *alias = (struct pmu){contexts, 2};
    pmus[0] = owner;
    pmus[1] = alias;
    contexts[3].ctx.pmu = owner;
    contexts[3].ctx.nr_events = 1;
    contexts[3].ctx.nr_cgroups = 1;
    contexts[3].ctx.is_active = true;
    contexts[3].unique_pmu = owner;
    contexts[3].cgrp = &boot_cgroup;
    retained = (struct perf_event){.ctx = &contexts[3].ctx, .cpu = 3,
        .state = PERF_EVENT_STATE_ACTIVE, .attach_state=PERF_ATTACH_CONTEXT,
        .cgrp=&boot_cgroup, .context_attached = true,
        .group_attached = true, .pmu_owned = true};
    retained.group_leader=&retained;
    contexts[0].cgrp=&boot_cgroup;
    executing_cpu = 0;
    target_online = true;
    irqs_off = false;
    frees = callbacks = hotplug_readers = quiesces = resumes = 0;
    rotations_started = rotations_stopped = 0;
}
int main(void)
{
    struct pmu owner, alias;
    initialize(&owner, &alias);
    assert(perf_event_release_kernel(&retained) == 0);
    assert(frees == 1 && callbacks == 1 && hotplug_readers == 0);
    initialize(&owner, &alias);
    for (unsigned cycle = 0; cycle < 20; cycle++) {
        executing_cpu = 3;
        irqs_off = true;
        perf_event_quiesce_cpu();
        assert(contexts[3].quiesced && !contexts[3].ctx.is_active);
        assert(!retained.pmu_owned && retained.context_attached && retained.group_attached);
        assert(!contexts[3].cgrp);
        target_online = false;
        assert(quiesces == cycle + 1 && rotations_stopped == cycle + 1);
        target_online = true;
        perf_event_resume_cpu(NULL);
        assert(retained.pmu_owned && contexts[3].ctx.is_active);
        assert(contexts[3].cgrp == &boot_cgroup);
        assert(resumes == cycle + 1 && rotations_started == cycle + 1);
    }
    perf_event_quiesce_cpu();
    target_online = false;
    irqs_off = false;
    executing_cpu = 0;
    _perf_event_disable(&retained);
    assert(retained.state==PERF_EVENT_STATE_OFF && !retained.pmu_owned);
    _perf_event_enable(&retained);
    assert(retained.state==PERF_EVENT_STATE_INACTIVE && !retained.pmu_owned);
    retained.state=PERF_EVENT_STATE_ERROR;
    _perf_event_enable(&retained);
    assert(retained.state==PERF_EVENT_STATE_INACTIVE);
    assert(perf_event_release_kernel(&retained) == 0);
    assert(frees == 1 && callbacks == 0 && hotplug_readers == 0);
    assert(contexts[3].ctx.nr_cgroups==0 && contexts[3].cgrp==NULL);
    assert(contexts[0].cgrp==&boot_cgroup);
    executing_cpu = 3;
    irqs_off = true;
    target_online = true;
    perf_event_resume_cpu(NULL);
    assert(!retained.pmu_owned && retained.freed);
    puts("PASS: online close, 20 retained restarts, offline close/enable/disable, target cgroup cleanup, shared-context deduplication");
    return 0;
}
