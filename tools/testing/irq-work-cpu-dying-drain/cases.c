struct fixture_work {
    struct irq_work work;
    unsigned callbacks, requeues;
    struct fixture_work *peer;
};

static unsigned observations, failures, fixture_nodes, fixture_requeues;
static bool frozen_context;

static void finite_callback(struct irq_work *work)
{
    struct fixture_work *fixture =
        (struct fixture_work *)((char *)work - offsetof(struct fixture_work, work));
    assert(work->flags == IRQ_WORK_BUSY);
    assert(interrupts_disabled && preempt_depth == callback_depth);
    fixture->callbacks++;
    assert(fixture->callbacks <= 16);
    if (fixture->peer)
        assert(irq_work_queue(&fixture->peer->work));
    if (fixture->callbacks <= fixture->requeues)
        assert(irq_work_queue(work));
}

static void reset(unsigned cpu)
{
    unsigned index;
    for (index = 0; index < 4; index++)
        irq_work_lists[index].first = NULL;
    executing_cpu = cpu;
    callback_depth = preempt_depth = 2;
    warning_count = 0;
    interrupts_disabled = true;
    hardirq_context = false;
    fixture_nodes = fixture_requeues = 0;
    frozen_context = false;
}

static void initialize(struct fixture_work *fixture, unsigned requeues)
{
    *fixture = (struct fixture_work) { .work.func = finite_callback,
                                     .requeues = requeues };
}

static unsigned pending(unsigned cpu)
{
    struct llist_node *node;
    unsigned count = 0;
    for (node = irq_work_lists[cpu].first; node; node = node->next) {
        assert(++count <= 4);
    }
    return count;
}

static void observation(const char *name, bool passes, unsigned callbacks,
                        unsigned expected_callbacks)
{
    passes = passes && preempt_depth == callback_depth;
    printf("{\"case\":\"%s\",\"cpu\":%u,\"frozen\":%s,"
           "\"nodes\":%u,\"requeues\":%u,\"callbacks\":%u,"
           "\"expected_callbacks\":%u,\"pending_nodes\":%u,"
           "\"warnings\":%u,\"preemption_balanced\":%s,\"passes\":%s}\n",
           name, executing_cpu, frozen_context ? "true" : "false",
           fixture_nodes, fixture_requeues, callbacks, expected_callbacks,
           pending(executing_cpu), warning_count,
           preempt_depth == callback_depth ? "true" : "false",
           passes ? "true" : "false");
    observations++;
    failures += !passes;
}

static void notifier_cases(unsigned cpu, bool frozen)
{
    const unsigned requeue_counts[] = {0, 1, 3, 8};
    unsigned action = CPU_DYING | (frozen ? CPU_TASKS_FROZEN : 0);
    struct fixture_work fixtures[3], other;
    unsigned count, index, choice;
    int result;

    reset(cpu);
    frozen_context = frozen;
    result = irq_work_cpu_notify(NULL, action, (void *)(uintptr_t)cpu);
    observation("empty", result == NOTIFY_OK && !pending(cpu) && !warning_count,
                0, 0);
    for (count = 1; count <= 3; count += 2) {
        for (choice = 0; choice < 4; choice++) {
            unsigned callbacks = 0;
            bool passes;
            reset(cpu);
            frozen_context = frozen;
            fixture_nodes = count;
            fixture_requeues = requeue_counts[choice];
            for (index = 0; index < count; index++) {
                initialize(&fixtures[index], requeue_counts[choice]);
                assert(irq_work_queue(&fixtures[index].work));
                assert(!irq_work_queue(&fixtures[index].work));
            }
            result = irq_work_cpu_notify(NULL, action, (void *)(uintptr_t)cpu);
            passes = result == NOTIFY_OK && !pending(cpu) && !warning_count;
            for (index = 0; index < count; index++) {
                callbacks += fixtures[index].callbacks;
                passes = passes && !fixtures[index].work.flags &&
                    fixtures[index].callbacks == requeue_counts[choice] + 1;
            }
            observation("finite-requeue", passes, callbacks,
                        count * (requeue_counts[choice] + 1));
            if (passes) {
                interrupts_disabled = false;
                for (index = 0; index < count; index++)
                    irq_work_sync(&fixtures[index].work);
            }
        }
    }

    reset(cpu);
    frozen_context = frozen;
    fixture_nodes = 3;
    for (index = 0; index < 3; index++)
        initialize(&fixtures[index], 0);
    fixtures[0].peer = &fixtures[1];
    fixtures[1].peer = &fixtures[2];
    assert(irq_work_queue(&fixtures[0].work));
    result = irq_work_cpu_notify(NULL, action, (void *)(uintptr_t)cpu);
    observation("callback-chain", result == NOTIFY_OK && !pending(cpu) &&
                !fixtures[0].work.flags && !fixtures[1].work.flags &&
                !fixtures[2].work.flags && fixtures[0].callbacks == 1 &&
                fixtures[1].callbacks == 1 && fixtures[2].callbacks == 1 &&
                !warning_count,
                fixtures[0].callbacks + fixtures[1].callbacks + fixtures[2].callbacks, 3);

    reset(cpu);
    frozen_context = frozen;
    fixture_nodes = 1;
    initialize(&fixtures[0], 0);
    initialize(&other, 0);
    executing_cpu = (cpu + 1) % 4;
    assert(irq_work_queue(&other.work));
    executing_cpu = cpu;
    assert(irq_work_queue(&fixtures[0].work));
    result = irq_work_cpu_notify(NULL, action, (void *)(uintptr_t)cpu);
    observation("other-cpu-retained", result == NOTIFY_OK && !pending(cpu) &&
                fixtures[0].callbacks == 1 && !fixtures[0].work.flags &&
                pending((cpu + 1) % 4) == 1 && !other.callbacks &&
                other.work.flags == IRQ_WORK_FLAGS && !warning_count, 1, 1);
}

static void boundary_cases(unsigned cpu)
{
    const unsigned other_actions[] = {0, 1, CPU_TASKS_FROZEN | 1};
    const unsigned requeue_counts[] = {0, 1, 3};
    struct fixture_work fixture;
    unsigned choice;
    int result;

    reset(cpu);
    fixture_nodes = 1;
    initialize(&fixture, 0);
    assert(irq_work_queue(&fixture.work));
    result = irq_work_cpu_notify(NULL, CPU_DYING,
                                (void *)(uintptr_t)((cpu + 1) % 4));
    observation("wrong-cpu", result == NOTIFY_DONE && warning_count == 1 &&
                fixture.callbacks == 0 && fixture.work.flags == IRQ_WORK_FLAGS &&
                pending(cpu) == 1, 0, 0);

    for (choice = 0; choice < 3; choice++) {
        reset(cpu);
        fixture_nodes = 1;
        frozen_context = (other_actions[choice] & CPU_TASKS_FROZEN) != 0;
        fixture_requeues = choice;
        initialize(&fixture, 0);
        assert(irq_work_queue(&fixture.work));
        result = irq_work_cpu_notify(NULL, other_actions[choice],
                                    (void *)(uintptr_t)cpu);
        observation("unrelated-action", result == NOTIFY_OK && !warning_count &&
                    fixture.callbacks == 0 && fixture.work.flags == IRQ_WORK_FLAGS &&
                    pending(cpu) == 1, 0, 0);
    }

    for (choice = 0; choice < 3; choice++) {
        bool passes;
        reset(cpu);
        fixture_nodes = 1;
        fixture_requeues = requeue_counts[choice];
        initialize(&fixture, requeue_counts[choice]);
        hardirq_context = true;
        assert(irq_work_queue(&fixture.work));
        irq_work_run();
        passes = fixture.callbacks == 1 && !warning_count;
        if (requeue_counts[choice])
            passes = passes && pending(cpu) == 1 && fixture.work.flags == IRQ_WORK_FLAGS;
        else
            passes = passes && !pending(cpu) && !fixture.work.flags;
        observation("hardirq-single-batch", passes, fixture.callbacks, 1);
        for (unsigned drain = 0; drain < requeue_counts[choice]; drain++)
            irq_work_run();
        assert(fixture.callbacks == requeue_counts[choice] + 1);
        assert(!pending(cpu) && !fixture.work.flags);
    }
}

static void guard_callback(struct irq_work *work)
{
    (void)work;
}

static void guard_case(unsigned cpu, bool hardirq_guard)
{
    pid_t child;
    int status;
    reset(cpu);
    fixture_nodes = 1;
    assert(fflush(NULL) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct irq_work work = { .func = guard_callback };
        if (!freopen("/dev/null", "w", stderr))
            _exit(97);
        if (!irq_work_queue(&work))
            _exit(98);
        if (hardirq_guard) {
            hardirq_context = false;
            irq_work_run();
        } else {
            interrupts_disabled = false;
            (void)irq_work_cpu_notify(NULL, CPU_DYING, (void *)(uintptr_t)cpu);
        }
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child);
    observation(hardirq_guard ? "hardirq-context-guard" : "disabled-irqs-guard",
                WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, 0, 0);
}

int main(void)
{
    struct rlimit core_limit = {0, 0};
    unsigned cpu;
    assert(setrlimit(RLIMIT_CORE, &core_limit) == 0);
    for (cpu = 0; cpu < 4; cpu++) {
        notifier_cases(cpu, false);
        notifier_cases(cpu, true);
        boundary_cases(cpu);
        guard_case(cpu, true);
        guard_case(cpu, false);
    }
    assert(observations == 124);
    return failures ? 1 : 0;
}
