#include "profiler/profiler.hpp"
#include <assert.h>
#include <thread>

static ProfilerNode root_node;
static thread_local ProfilerNode* current_node = 0;

void profilerScopeBegin(const char* name) {
	if (current_node == 0) {
		current_node = &root_node;
	}

	current_node->mtx.lock();
	auto it = current_node->nodes.find(name);
	if (it == current_node->nodes.end()) {
		ProfilerNode* pnode = new ProfilerNode;
		pnode->name = name;
		pnode->parent = current_node;

		it = current_node->nodes.insert(std::make_pair(std::string(name), std::unique_ptr<ProfilerNode>(pnode))).first;
	}
	current_node->mtx.unlock();

	current_node = it->second.get();
	
	current_node->mtx.lock();
	current_node->count++;
	QueryPerformanceCounter(&current_node->_start);
	current_node->mtx.unlock();
}

void profilerScopeEnd() {
	LARGE_INTEGER _start = current_node->_start;
	LARGE_INTEGER _end;
	LARGE_INTEGER _freq;
	QueryPerformanceCounter(&_end);
	QueryPerformanceFrequency(&_freq);
	uint64_t elapsedMicrosec = ((_end.QuadPart - _start.QuadPart) * 1000000LL) / _freq.QuadPart;
	//double sec = (float)elapsedMicrosec * .000001f;
	double ms = (float)elapsedMicrosec * .001f;

	current_node->mtx.lock();
	current_node->total_ms += ms;
	current_node->mtx.unlock();

	assert(current_node->parent);
	current_node = current_node->parent;
}

bool profilerDump(const char* filename) {
	std::ofstream strm(filename, std::ios::out | std::ios::trunc);
	if (!strm) {
		return false;
	}

	strm << "ScopeName|Count|MsecAverage|MsecTotal|Percentage\n";

	root_node.calcPercentages();
	root_node.dump(strm);

	return true;
}