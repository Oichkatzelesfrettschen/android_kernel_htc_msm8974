static void do_perf_sw_event(u32 type, u32 event_id, u64 count,
                             struct perf_sample_data *data,
                             struct pt_regs *registers)
{
    assert(type == PERF_TYPE_SOFTWARE && event_id == 42 && count == 7);
    assert(data->address == 0x1234 && preemption_depth > 0);
    callback_calls++;
    if (nested_mode) {
        unsigned mode = nested_mode;
        int saved_context = context_kind;
        nested_mode = 0;
        if (mode == 2)
            context_kind = (context_kind + 1) % 4;
        __perf_sw_event(42, 7, registers, 0x1234);
        context_kind = saved_context;
    }
}

int main(void)
{
    unsigned failures = 0, cases = 0;
    struct pt_regs registers = { 0 };
    const char *modes[] = { "plain", "occupied", "nested-same", "nested-different" };
    for (int context = 0; context < 4; context++) {
        for (int entry = 0; entry <= 5; entry += 5) {
            for (unsigned mode = 0; mode < 4; mode++) {
                memset(&swevent_htable, 0, sizeof(swevent_htable));
                context_kind = context;
                preemption_depth = entry;
                disable_calls = enable_calls = sample_calls = callback_calls = 0;
                acquired_slots = released_slots = 0;
                nested_mode = mode == 2 ? 1 : mode == 3 ? 2 : 0;
                if (mode == 1)
                    swevent_htable.recursion[context] = 1;
                __perf_sw_event(42, 7, &registers, 0x1234);
                bool recursion_balanced = true;
                for (int slot = 0; slot < 4; slot++) {
                    int expected = mode == 1 && slot == context ? 1 : 0;
                    if (swevent_htable.recursion[slot] != expected)
                        recursion_balanced = false;
                }
                unsigned expected_callbacks = mode == 1 ? 0 : mode == 3 ? 2 : 1;
                bool passes = preemption_depth == entry && disable_calls == enable_calls &&
                              recursion_balanced && callback_calls == expected_callbacks &&
                              sample_calls == expected_callbacks &&
                              acquired_slots == expected_callbacks &&
                              released_slots == expected_callbacks;
                printf("{\"context\":%d,\"mode\":\"%s\",\"entry_depth\":%d,"
                       "\"exit_depth\":%d,\"disable_calls\":%u,\"enable_calls\":%u,"
                       "\"callbacks\":%u,\"acquired_slots\":%u,\"released_slots\":%u,"
                       "\"recursion_balanced\":%s,\"passes\":%s}\n",
                       context, modes[mode], entry, preemption_depth,
                       disable_calls, enable_calls, callback_calls, acquired_slots, released_slots,
                       recursion_balanced ? "true" : "false", passes ? "true" : "false");
                cases++;
                if (!passes)
                    failures++;
            }
        }
    }
    assert(cases == 32);
    return failures ? 1 : 0;
}
