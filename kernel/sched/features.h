/* kernel/sched/features.h */

/* Defina aqui suas preferências */
#define SCHED_FEAT_GENTLE_FAIR_SLEEPERS      1
#define SCHED_FEAT_START_DEBIT               1
#define SCHED_FEAT_NEXT_BUDDY                0
#define SCHED_FEAT_LAST_BUDDY                1
#define SCHED_FEAT_CACHE_HOT_BUDDY           1
#define SCHED_FEAT_ARCH_POWER                0
#define SCHED_FEAT_HRTICK                    0
#define SCHED_FEAT_DOUBLE_TICK               0
#define SCHED_FEAT_LB_BIAS                   1
#define SCHED_FEAT_OWNER_SPIN                1
#define SCHED_FEAT_NONTASK_POWER             1
#define SCHED_FEAT_TTWU_QUEUE                1
#define SCHED_FEAT_FORCE_SD_OVERLAP          0
#define SCHED_FEAT_RT_RUNTIME_SHARE          1
#define SCHED_FEAT_LB_MIN                    0

/* Macro de acesso */
#define sched_feat(x) (SCHED_FEAT_##x)
