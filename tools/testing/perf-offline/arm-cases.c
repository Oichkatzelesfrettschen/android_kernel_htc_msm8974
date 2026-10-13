int main(void)
{
    struct arm_pmu pmu = {.get_hw_events=get_hardware,
        .test_set_event_constraints=msm_l2_test_set_ev_constraint,
        .clear_event_constraints=msm_l2_clear_ev_constraint,
        .get_event_idx=allocate_slot, .disable=disable_counter};
    struct perf_event first = {.pmu=&pmu.pmu, .hw.idx=-1,
        .attr.config=0x10120, .cpu=-1};
    struct perf_event second = first;
    struct perf_event duplicate = first;
    duplicate.cpu=3;
    for (unsigned cycle=0; cycle<20; cycle++) {
        assert(armpmu_add(&first, PERF_EF_START)==0);
        assert(armpmu_add(&second, PERF_EF_START)==0);
        assert(l2_pmu_constraints.users[0]==2);
        assert(armpmu_add(&duplicate, PERF_EF_START)==-EPERM);
        assert(duplicate.hw.idx==-1 && l2_pmu_constraints.users[0]==2);
        assert(duplicate.attr.constraint_duplicate);
        armpmu_del(&first, 0);
        assert(first.hw.idx==-1 && l2_pmu_constraints.users[0]==1);
        assert(l2_pmu_constraints.pmu_bitmap & 1);
        armpmu_del(&second, 0);
        assert(second.hw.idx==-1 && l2_pmu_constraints.users[0]==0);
        assert(hardware.used_mask[0]==0 && l2_pmu_constraints.pmu_bitmap==0);
        assert(hardware.events[0]==NULL && hardware.events[1]==NULL);
    }
    fail_allocation=true;
    assert(armpmu_add(&first, PERF_EF_START)==-ENOSPC);
    assert(l2_pmu_constraints.users[0]==0 && l2_pmu_constraints.pmu_bitmap==0);
    fail_allocation=false;
    first.attr.config=0x10ff0;
    second.attr.config=0x10010;
    assert(armpmu_add(&first, PERF_EF_START)==0);
    assert(armpmu_add(&second, PERF_EF_START)==-EPERM);
    assert(l2_pmu_constraints.users[0]==1);
    armpmu_del(&first, 0);
    first.attr.config=L2CYCLE_CTR_RAW_CODE;
    assert(armpmu_add(&first, PERF_EF_START)==0);
    armpmu_del(&first, 0);
    assert(l2_pmu_constraints.pmu_bitmap==0);
    assert(stop_calls==42 && start_calls==42 && page_updates==84);
    first.attr.config=0x10000;
    second.attr.config=0x20000;
    assert(msm_test_set_ev_constraint(&first)==1);
    assert(msm_test_set_ev_constraint(&second)==1);
    assert(pmu_bitmap==((1ULL<<32)|1));
    assert(msm_test_set_ev_constraint(&second)==-EPERM);
    assert(msm_clear_ev_constraint(&first)==1 && pmu_bitmap==(1ULL<<32));
    assert(msm_clear_ev_constraint(&second)==1 && pmu_bitmap==0);
    first.attr.config=0x30000;
    assert(msm_test_set_ev_constraint(&first)==-EINVAL && pmu_bitmap==0);
    assert(msm_clear_ev_constraint(&first)==-EINVAL && pmu_bitmap==0);
    puts("PASS: ARM add/del, 20 L2 sharing cycles, duplicate exclusion, allocation rollback, cycle counter, Krait 64-bit constraints");
    return 0;
}
