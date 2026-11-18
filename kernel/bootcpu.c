#include <bootcpu.h>
int
bootcpucheck(const Dtinfo *info, u64 current, Bootcpus *out)
{
  u32 i, j, found = 0, method, state;
  if (!out)
    return -1;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = 0;
  if (!info || !info->cpus || info->cpus > DT_CPUS)
    return -1;
  method = info->cpu[0].method;
  for (i = 0; i < info->cpus; i++) {
    if (info->cpu[i].method != method)
      return -1;
    for (j = 0; j < i; j++)
      if (info->cpu[j].id == info->cpu[i].id)
        return -1;
    if (info->cpu[i].id == current)
      found++;
  }
  if (found != 1 || hal_cpu_probe(method))
    return -1;
  for (i = 0; i < info->cpus; i++) {
    if (hal_cpu_status(method, info->cpu[i].id, &state) ||
        state != (info->cpu[i].id == current ? CPU_RUNNING : CPU_STOPPED))
      goto failed;
    out->cpu[i].id = info->cpu[i].id;
    out->cpu[i].state = state;
    out->cpu[i].intc = info->cpu[i].intc;
  }
  out->current = current;
  out->count = info->cpus;
  out->method = method;
  return 0;
failed:
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = 0;
  return -1;
}
