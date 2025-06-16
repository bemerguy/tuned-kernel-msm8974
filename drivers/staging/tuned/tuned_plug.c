/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* tuned hotplugger by fbs (heiler.bemerguy@gmail.com) */

#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/rq_stats.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/lcd_notify.h>
#include <linux/mm.h>

static struct notifier_block lcd_notif;

unsigned int tunedplug_active __read_mostly = 1;
module_param(tunedplug_active, uint, 0644);

#define DEF_SAMPLING	HZ/50

/* frequency threshold to wake one more cpu */
//this should be higher than the default governor highfreq
#define PMAX 1267200

static unsigned long sampling_time = DEF_SAMPLING;

/* up threshold. lower means more delay */
static const int u[] = { -6, -4, -2 };

/* down threshold. higher means more delay */

static const int d[] = { 5, 9, 14 };

bool displayon = true;

static int state[NR_CPUS-1] = {0};
/* 123 are cpu cores. */

static void down_one(void){
        unsigned int i;
	for_each_online_cpu(i) {
		if (i) {
			if (state[i-1] > d[i-1]) {
                                cpu_down(i);
                                pr_info("tunedplug: DOWN cpu %d. (%d > %d)\n",
					i, state[i-1], d[i-1]);
				state[i-1]=d[2];
				return;
                	}
                	else state[i-1]++;
		}
        }
}
static void up_one(void){
        unsigned int i;
        for (i = NR_CPUS-1; i > 0; i--) {
                if (!cpu_online(i)) {
                        if (state[i-1] < u[i-1]) {
                                struct cpufreq_policy policy, *p = &policy;

                                pr_info("tunedplug: UP cpu %d. (%d < %d)\n",
					i, state[i-1], u[i-1]);

                                cpu_up(i);

                                if (unlikely(cpufreq_get_policy(&policy, i) != 0)) {
                                        pr_info("tunedplug: no policy for cpu %d ?", i);
				}
                                else {
                                        __cpufreq_driver_target(p, p->max, CPUFREQ_RELATION_H);
					state[i-1]=u[0];
				}
                        }
                        else state[i-1]--;
                        return;
                }
        }
}

static void tunedplug_work_fn(struct work_struct *work)
{
	unsigned int i, status[3] = { 0 };
	struct cpufreq_policy policy;
        struct delayed_work *dwork = to_delayed_work(work);

	queue_delayed_work(system_nrt_freezable_wq, dwork, sampling_time);

        if (!tunedplug_active)
                return;

	if (!displayon && (sampling_time < HZ))
		sampling_time++;

#define TMAXFREQ status[0]
#define TLOWFREQ status[1]
#define TONLINE status[2]

	for_each_online_cpu(i) {
			TONLINE++;
			if (cpufreq_get_policy(&policy, i) != 0)
				continue;
			if (policy.cur > PMAX) TMAXFREQ++;
			else if (i && policy.cur <= policy.min) TLOWFREQ++;
	}

//	pr_info("tunedplug ON=%d. LOW=%d. MAX=%d. cpu1: %d cpu2: %d cpu3: %d\n",
//		TONLINE, TLOWFREQ, TMAXFREQ, state[0], state[1], state[2]);

	if (TMAXFREQ && TONLINE<NR_CPUS) up_one();
	else if (TLOWFREQ) down_one();

}
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
        switch (event)
        {
                case LCD_EVENT_OFF_END:
			displayon = false;
                        break;

                case LCD_EVENT_ON_START:
			displayon = true;
	                sampling_time = DEF_SAMPLING;
                        break;

                default:
                        break;
        }
        return 0;
}

static void initnotifier(void)
{
        lcd_notif.notifier_call = lcd_notifier_callback;
        if (lcd_register_client(&lcd_notif) != 0)
                pr_err("%s: Failed to register lcd callback\n", __func__);

}

static int __init tuned_plug_init(void)
{

	struct delayed_work *dwork;

	sampling_time = DEF_SAMPLING;

        dwork = kmalloc(sizeof(*dwork), GFP_KERNEL);
        INIT_DELAYED_WORK_DEFERRABLE(dwork, tunedplug_work_fn);
        queue_delayed_work(system_nrt_freezable_wq, dwork, 30000);

	initnotifier();

	return 0;
}

MODULE_AUTHOR("Heiler Bemerguy <heiler.bemerguy@gmail.com>");
MODULE_DESCRIPTION("'tuned_plug' - A simple cpu hotplug driver");
MODULE_LICENSE("GPL");

late_initcall(tuned_plug_init);
