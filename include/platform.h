#pragma once

namespace fw::plat {

void bind_thread_to_core(int core_id);
void set_high_res_timer();
void reset_high_res_timer();
int  logical_cores();
int  physical_cores();

} // namespace fw::plat
