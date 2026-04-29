time = time or {}

time.time = __c_time
time.sleep = __c_time_sleep

return time
