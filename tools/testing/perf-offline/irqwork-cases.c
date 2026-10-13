int main(void)
{
    struct irq_work first={.flags=3, .func=pending_callback};
    struct irq_work second={.flags=3, .func=pending_callback};
    first.llnode.next=&second.llnode;
    irq_work_list.first=&first.llnode;
    irq_disabled=true;
    hardirq=false;
    assert(irq_work_cpu_notify(NULL, CPU_DYING|CPU_TASKS_FROZEN, (void *)3)==NOTIFY_OK);
    assert(callbacks==2 && !first.flags && !second.flags && !irq_work_list.first);
    irq_disabled=false;
    irq_work_sync(&first);
    irq_work_sync(&second);
    irq_disabled=true;
    hardirq=true;
    first.flags=3;
    first.llnode.next=NULL;
    irq_work_list.first=&first.llnode;
    irq_work_run();
    assert(callbacks==3 && !first.flags);
    puts("PASS: frozen CPU_DYING drains pending owners; sync completes; hardirq runner remains active");
    return 0;
}
