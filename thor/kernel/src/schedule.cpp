
#include "kernel.hpp"

namespace traits = frigg::traits;

namespace thor {

LazyInitializer<ThreadQueue> scheduleQueue;

void switchThread(UnsafePtr<Thread, KernelAlloc> thread) {
	auto cpu_context = (CpuContext *)thorRtGetCpuContext();
	cpu_context->currentThread.reset();
	thread->activate();
	cpu_context->currentThread = SharedPtr<Thread, KernelAlloc>(thread);
}

void doSchedule() {
	ASSERT(!scheduleQueue->empty());
	SharedPtr<Thread, KernelAlloc> thread = scheduleQueue->removeFront();
	switchThread(thread);
	
	if(!getCurrentThread()->isKernelThread()) {
		thorRtFullReturn();
	}else{
		thorRtFullReturnToKernel();
	}
}

void enqueueInSchedule(SharedPtr<Thread, KernelAlloc> &&thread) {
	scheduleQueue->addBack(traits::move(thread));
}

} // namespace thor
