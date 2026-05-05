/*
 * dispatcher_generated.c — Auto-generated from include/nt_syscalls.def
 * DO NOT EDIT BY HAND — run scripts/gen_dispatcher.py --generate
 * Contains the switch bodies for both dispatcher entry points.
 * Included from dispatcher.c via #define + #include.
 */

#ifndef DISPATCHER_GENERATED_C
#define DISPATCHER_GENERATED_C

#if defined(DISPATCHER_C_BODY)
switch (nr) {
    case 0x05: /* NtCallbackReturn */
    {
        result = handler_NtCallbackReturn();
        break;
    }
    case 0x07: /* NtQueryInformationProcess */
    {
        uint64_t h_retLen = read_guest_stack(1);

        int status = read_guest_ptr(arg3, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_retLen, NULL, NULL, "return_length");
        if (status != 0) return (uint64_t)status;

        result = handler_NtQueryInformationProcess(arg1, arg2, arg3, arg4, h_retLen);
        break;
    }
    case 0x0F: /* NtClose */
    {
        result = handler_NtClose(arg1);
        break;
    }
    case 0x18: /* NtAllocateVirtualMemory */
    {
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        uint64_t h_regionSz = 0;
        void *p_regionSz = NULL;

        if (dispatch_ptr_inout(arg2, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout(arg4, &h_regionSz, &p_regionSz, "h_regionSz", &result) != 0) break;

        result = handler_NtAllocateVirtualMemory(arg1, &h_baseAddr, arg3, &h_regionSz, read_guest_stack(1), read_guest_stack(2));
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_regionSz) *(uint64_t *)p_regionSz = h_regionSz;
        break;
    }
    case 0x19: /* NtFreeVirtualMemory */
    {
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        uint64_t h_regionSz = 0;
        void *p_regionSz = NULL;

        if (dispatch_ptr_inout(arg2, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout(arg3, &h_regionSz, &p_regionSz, "h_regionSz", &result) != 0) break;

        result = handler_NtFreeVirtualMemory(arg1, &h_baseAddr, &h_regionSz, arg4);
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_regionSz) *(uint64_t *)p_regionSz = h_regionSz;
        break;
    }
    case 0x24: /* NtGetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return (uint64_t)status;

        result = handler_NtGetContextThread(arg1, arg2);
        break;
    }
    case 0x26: /* NtSetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return (uint64_t)status;

        result = handler_NtSetContextThread(arg1, arg2);
        break;
    }
    case 0x28: /* NtMapViewOfSection */
    {
        uint64_t h_sectionOff = read_guest_stack(1);
        uint64_t h_viewSz = read_guest_stack(2);
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        void *p_sectionOff = NULL;
        void *p_viewSz = NULL;

        if (dispatch_ptr_inout(arg3, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout(h_sectionOff, &h_sectionOff, &p_sectionOff, "section_offset", &result) != 0) break;
        if (dispatch_ptr_inout(h_viewSz, &h_viewSz, &p_viewSz, "view_size", &result) != 0) break;

        result = handler_NtMapViewOfSection(arg1, arg2, &h_baseAddr, arg4, read_guest_stack(1), &h_sectionOff, &h_viewSz, read_guest_stack(4), read_guest_stack(5), read_guest_stack(6));
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_sectionOff) *(uint64_t *)p_sectionOff = h_sectionOff;
        if (p_viewSz) *(uint64_t *)p_viewSz = h_viewSz;
        break;
    }
    case 0x29: /* NtUnmapViewOfSection */
    {
        result = handler_NtUnmapViewOfSection(arg1, arg2);
        break;
    }
    case 0x2A: /* NtTerminateProcess */
    {
        result = handler_NtTerminateProcess(arg1, arg2);
        break;
    }
    case 0x3C: /* NtReadFile */
    {
        uint64_t h_buffer = read_guest_stack(1);
        uint64_t h_bytesRead = read_guest_stack(4);

        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_bytesRead, NULL, NULL, "bytes_read");
        if (status != 0) return (uint64_t)status;

        result = handler_NtReadFile(arg1, arg2, arg3, arg4, h_buffer, read_guest_stack(2), read_guest_stack(3), h_bytesRead);
        break;
    }
    case 0x3D: /* NtWriteFile */
    {
        uint64_t h_buffer = read_guest_stack(1);
        uint64_t h_bytesWritten = read_guest_stack(4);

        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_bytesWritten, NULL, NULL, "bytes_written");
        if (status != 0) return (uint64_t)status;

        result = handler_NtWriteFile(arg1, arg2, arg3, arg4, h_buffer, read_guest_stack(2), read_guest_stack(3), h_bytesWritten);
        break;
    }
    case 0x44: /* NtCreateMutex */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateMutex(&h_handle, arg2, arg3);
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x48: /* NtCreateEvent */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4, read_guest_stack(1));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x4A: /* NtCreateSection */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        uint64_t h_maxSz = 0;
        void *p_maxSz = NULL;

        if (dispatch_ptr_inout(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;
        if (dispatch_ptr_inout(arg4, &h_maxSz, &p_maxSz, "h_maxSz", &result) != 0) break;

        result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_maxSz, read_guest_stack(1), read_guest_stack(2), read_guest_stack(3));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        if (p_maxSz) *(uint64_t *)p_maxSz = h_maxSz;
        break;
    }
    case 0x4E: /* NtCreateThreadEx */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4, read_guest_stack(1), read_guest_stack(2), read_guest_stack(3), read_guest_stack(4), read_guest_stack(5), read_guest_stack(6), read_guest_stack(7));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x4F: /* NtOpenFile */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;
        int status = read_guest_ptr(arg3, NULL, NULL, "object_attributes");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg4, NULL, NULL, "io_status_block");
        if (status != 0) return (uint64_t)status;

        result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4, read_guest_stack(1), read_guest_stack(2));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x55: /* NtQueryPerformanceCounter */
    {
        uint64_t h_counter = 0;
        void *p_counter = NULL;

        if (dispatch_ptr_inout(arg1, &h_counter, &p_counter, "h_counter", &result) != 0) break;

        result = handler_NtQueryPerformanceCounter((PVOID)&h_counter);
        if (p_counter) *(uint64_t *)p_counter = h_counter;
        break;
    }
    case 0x56: /* NtQueryPerformanceFrequency */
    {
        uint64_t h_freq = 0;
        void *p_freq = NULL;

        if (dispatch_ptr_inout(arg1, &h_freq, &p_freq, "h_freq", &result) != 0) break;

        result = handler_NtQueryPerformanceFrequency((PVOID)&h_freq);
        if (p_freq) *(uint64_t *)p_freq = h_freq;
        break;
    }
    case 0x5C: /* NtSetEvent */
    {
        uint64_t h_prev = 0;
        void *p_prev = NULL;

        if (dispatch_ptr_inout(arg2, &h_prev, &p_prev, "h_prev", &result) != 0) break;

        result = handler_NtSetEvent(arg1, (PVOID)&h_prev);
        if (p_prev) *(uint64_t *)p_prev = h_prev;
        break;
    }
    case 0x5E: /* NtResetEvent */
    {
        uint64_t h_prev = 0;
        void *p_prev = NULL;

        if (dispatch_ptr_inout(arg2, &h_prev, &p_prev, "h_prev", &result) != 0) break;

        result = handler_NtResetEvent(arg1, (PVOID)&h_prev);
        if (p_prev) *(uint64_t *)p_prev = h_prev;
        break;
    }
    case 0x09: /* NtQuerySystemTime */
    {
        uint64_t h_ftVal = 0;
        void *p_ftVal = NULL;

        if (dispatch_ptr_inout(arg1, &h_ftVal, &p_ftVal, "h_ftVal", &result) != 0) break;

        result = handler_NtQuerySystemTime((PVOID)&h_ftVal);
        if (p_ftVal) *(uint64_t *)p_ftVal = h_ftVal;
        break;
    }
    case 0x1A: /* NtDelayExecution */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;

        if (dispatch_ptr_inout(arg2, &h_timeout, &p_timeout, "h_timeout", &result) != 0) break;

        result = handler_NtDelayExecution(arg1, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }
    case 0x1E: /* NtReleaseMutex */
    {
        result = handler_NtReleaseMutex(arg1, arg2);
        break;
    }
    case 0x03: /* NtWaitForSingleObject */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;

        if (dispatch_ptr_inout(arg3, &h_timeout, &p_timeout, "h_timeout", &result) != 0) break;

        result = handler_NtWaitForSingleObject(arg1, arg2, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }

    default:
    {
        char buf[39];
        format_err_unhandled_syscall(buf, nr);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
    }
    result = STATUS_NOT_IMPLEMENTED;
    break;}
#elif defined(DISPATCHER_LEGACY_BODY)
switch (nt_nr) {
    case 0x05: /* NtCallbackReturn */
    {
        result = handler_NtCallbackReturn();
        break;
    }
    case 0x07: /* NtQueryInformationProcess */
    {
        uint64_t h_retLen = read_guest_stack_ctx(ctx, 1);

        int status = read_guest_ptr(arg3, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_retLen, NULL, NULL, "return_length");
        if (status != 0) return status;

        result = handler_NtQueryInformationProcess(arg1, arg2, arg3, arg4, h_retLen);
        break;
    }
    case 0x0F: /* NtClose */
    {
        result = handler_NtClose(arg1);
        break;
    }
    case 0x18: /* NtAllocateVirtualMemory */
    {
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        uint64_t h_regionSz = 0;
        void *p_regionSz = NULL;

        if (dispatch_ptr_inout_ctx(arg2, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout_ctx(arg4, &h_regionSz, &p_regionSz, "h_regionSz", &result) != 0) break;

        result = handler_NtAllocateVirtualMemory(arg1, &h_baseAddr, arg3, &h_regionSz, read_guest_stack_ctx(ctx, 1), read_guest_stack_ctx(ctx, 2));
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_regionSz) *(uint64_t *)p_regionSz = h_regionSz;
        break;
    }
    case 0x19: /* NtFreeVirtualMemory */
    {
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        uint64_t h_regionSz = 0;
        void *p_regionSz = NULL;

        if (dispatch_ptr_inout_ctx(arg2, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout_ctx(arg3, &h_regionSz, &p_regionSz, "h_regionSz", &result) != 0) break;

        result = handler_NtFreeVirtualMemory(arg1, &h_baseAddr, &h_regionSz, arg4);
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_regionSz) *(uint64_t *)p_regionSz = h_regionSz;
        break;
    }
    case 0x24: /* NtGetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return status;

        result = handler_NtGetContextThread(arg1, arg2);
        break;
    }
    case 0x26: /* NtSetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return status;

        result = handler_NtSetContextThread(arg1, arg2);
        break;
    }
    case 0x28: /* NtMapViewOfSection */
    {
        uint64_t h_sectionOff = read_guest_stack_ctx(ctx, 1);
        uint64_t h_viewSz = read_guest_stack_ctx(ctx, 2);
        uint64_t h_baseAddr = 0;
        void *p_baseAddr = NULL;
        void *p_sectionOff = NULL;
        void *p_viewSz = NULL;

        if (dispatch_ptr_inout_ctx(arg3, &h_baseAddr, &p_baseAddr, "h_baseAddr", &result) != 0) break;
        if (dispatch_ptr_inout_ctx(h_sectionOff, &h_sectionOff, &p_sectionOff, "section_offset", &result) != 0) break;
        if (dispatch_ptr_inout_ctx(h_viewSz, &h_viewSz, &p_viewSz, "view_size", &result) != 0) break;

        result = handler_NtMapViewOfSection(arg1, arg2, &h_baseAddr, arg4, read_guest_stack_ctx(ctx, 1), &h_sectionOff, &h_viewSz, read_guest_stack_ctx(ctx, 4), read_guest_stack_ctx(ctx, 5), read_guest_stack_ctx(ctx, 6));
        if (p_baseAddr) *(uint64_t *)p_baseAddr = h_baseAddr;
        if (p_sectionOff) *(uint64_t *)p_sectionOff = h_sectionOff;
        if (p_viewSz) *(uint64_t *)p_viewSz = h_viewSz;
        break;
    }
    case 0x29: /* NtUnmapViewOfSection */
    {
        result = handler_NtUnmapViewOfSection(arg1, arg2);
        break;
    }
    case 0x2A: /* NtTerminateProcess */
    {
        result = handler_NtTerminateProcess(arg1, arg2);
        break;
    }
    case 0x3C: /* NtReadFile */
    {
        uint64_t h_buffer = read_guest_stack_ctx(ctx, 1);
        uint64_t h_bytesRead = read_guest_stack_ctx(ctx, 4);

        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_bytesRead, NULL, NULL, "bytes_read");
        if (status != 0) return status;

        result = handler_NtReadFile(arg1, arg2, arg3, arg4, h_buffer, read_guest_stack_ctx(ctx, 2), read_guest_stack_ctx(ctx, 3), h_bytesRead);
        break;
    }
    case 0x3D: /* NtWriteFile */
    {
        uint64_t h_buffer = read_guest_stack_ctx(ctx, 1);
        uint64_t h_bytesWritten = read_guest_stack_ctx(ctx, 4);

        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_bytesWritten, NULL, NULL, "bytes_written");
        if (status != 0) return status;

        result = handler_NtWriteFile(arg1, arg2, arg3, arg4, h_buffer, read_guest_stack_ctx(ctx, 2), read_guest_stack_ctx(ctx, 3), h_bytesWritten);
        break;
    }
    case 0x44: /* NtCreateMutex */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateMutex(&h_handle, arg2, arg3);
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x48: /* NtCreateEvent */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4, read_guest_stack_ctx(ctx, 1));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x4A: /* NtCreateSection */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        uint64_t h_maxSz = 0;
        void *p_maxSz = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;
        if (dispatch_ptr_inout_ctx(arg4, &h_maxSz, &p_maxSz, "h_maxSz", &result) != 0) break;

        result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_maxSz, read_guest_stack_ctx(ctx, 1), read_guest_stack_ctx(ctx, 2), read_guest_stack_ctx(ctx, 3));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        if (p_maxSz) *(uint64_t *)p_maxSz = h_maxSz;
        break;
    }
    case 0x4E: /* NtCreateThreadEx */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;

        result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4, read_guest_stack_ctx(ctx, 1), read_guest_stack_ctx(ctx, 2), read_guest_stack_ctx(ctx, 3), read_guest_stack_ctx(ctx, 4), read_guest_stack_ctx(ctx, 5), read_guest_stack_ctx(ctx, 6), read_guest_stack_ctx(ctx, 7));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x4F: /* NtOpenFile */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_handle, &p_handle, "h_handle", &result) != 0) break;
        int status = read_guest_ptr(arg3, NULL, NULL, "object_attributes");
        if (status != 0) return status;
        status = read_guest_ptr(arg4, NULL, NULL, "io_status_block");
        if (status != 0) return status;

        result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4, read_guest_stack_ctx(ctx, 1), read_guest_stack_ctx(ctx, 2));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }
    case 0x55: /* NtQueryPerformanceCounter */
    {
        uint64_t h_counter = 0;
        void *p_counter = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_counter, &p_counter, "h_counter", &result) != 0) break;

        result = handler_NtQueryPerformanceCounter((PVOID)&h_counter);
        if (p_counter) *(uint64_t *)p_counter = h_counter;
        break;
    }
    case 0x56: /* NtQueryPerformanceFrequency */
    {
        uint64_t h_freq = 0;
        void *p_freq = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_freq, &p_freq, "h_freq", &result) != 0) break;

        result = handler_NtQueryPerformanceFrequency((PVOID)&h_freq);
        if (p_freq) *(uint64_t *)p_freq = h_freq;
        break;
    }
    case 0x5C: /* NtSetEvent */
    {
        uint64_t h_prev = 0;
        void *p_prev = NULL;

        if (dispatch_ptr_inout_ctx(arg2, &h_prev, &p_prev, "h_prev", &result) != 0) break;

        result = handler_NtSetEvent(arg1, (PVOID)&h_prev);
        if (p_prev) *(uint64_t *)p_prev = h_prev;
        break;
    }
    case 0x5E: /* NtResetEvent */
    {
        uint64_t h_prev = 0;
        void *p_prev = NULL;

        if (dispatch_ptr_inout_ctx(arg2, &h_prev, &p_prev, "h_prev", &result) != 0) break;

        result = handler_NtResetEvent(arg1, (PVOID)&h_prev);
        if (p_prev) *(uint64_t *)p_prev = h_prev;
        break;
    }
    case 0x09: /* NtQuerySystemTime */
    {
        uint64_t h_ftVal = 0;
        void *p_ftVal = NULL;

        if (dispatch_ptr_inout_ctx(arg1, &h_ftVal, &p_ftVal, "h_ftVal", &result) != 0) break;

        result = handler_NtQuerySystemTime((PVOID)&h_ftVal);
        if (p_ftVal) *(uint64_t *)p_ftVal = h_ftVal;
        break;
    }
    case 0x1A: /* NtDelayExecution */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;

        if (dispatch_ptr_inout_ctx(arg2, &h_timeout, &p_timeout, "h_timeout", &result) != 0) break;

        result = handler_NtDelayExecution(arg1, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }
    case 0x1E: /* NtReleaseMutex */
    {
        result = handler_NtReleaseMutex(arg1, arg2);
        break;
    }
    case 0x03: /* NtWaitForSingleObject */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;

        if (dispatch_ptr_inout_ctx(arg3, &h_timeout, &p_timeout, "h_timeout", &result) != 0) break;

        result = handler_NtWaitForSingleObject(arg1, arg2, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }

    default:
    {
        char buf[39];
        format_err_unhandled_syscall(buf, syscall_number);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
    }
    result = STATUS_NOT_IMPLEMENTED;
    break;}
#else
#error "Define DISPATCHER_C_BODY or DISPATCHER_LEGACY_BODY before including this file"
#endif

#endif /* DISPATCHER_GENERATED_C */
