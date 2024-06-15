#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <mutex>
#include <stack>
#include <stdint.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


struct ProfilerNode {
	std::string name;
	ProfilerNode* parent;
	std::unordered_map<std::string, std::unique_ptr<ProfilerNode>> nodes;
	std::mutex mtx;

	int count = 0;
	double total_ms = .0f;
	double percentage = 100.0f;
	LARGE_INTEGER _start;

	void calcPercentages() {
		if (count > 0) {
			for (auto& it : nodes) {
				it.second->percentage = it.second->total_ms / total_ms * 100.0;
			}
		}

		for (auto& it : nodes) {
			it.second->calcPercentages();
		}
	}

	void dump(std::ofstream& strm) {
		// count == 0 is the root node, ok to skip
		if (count > 0) {
			std::stack<ProfilerNode*> node_stack;
			ProfilerNode* current = parent;
			while (current && current->count > 0) {
				std::string full_name;
				node_stack.push(current);
				current = current->parent;
			}

			std::string full_name;
			while (!node_stack.empty()) {
				ProfilerNode* n = node_stack.top();
				node_stack.pop();
				full_name += n->name;
				full_name += "/";
			}
			full_name += name;

			double average_ms = total_ms / (double)count;

			strm << full_name << "|" << count << "|" << average_ms << "|" << total_ms << "|" << percentage << std::endl;
		}
		for (auto& it : nodes) {
			it.second->dump(strm);
		}
	}
};


void profilerScopeBegin(const char* name);
void profilerScopeEnd();

bool profilerDump(const char* filename);


class ProfilerScopedObject {
public:
	ProfilerScopedObject(const char* name) {
		profilerScopeBegin(name);
	}
	~ProfilerScopedObject() {
		profilerScopeEnd();
	}
};


#define PROF_CONCAT(a, b) PROF_CONCAT_INNER(a, b)
#define PROF_CONCAT_INNER(a, b) a ## b
#define PROF_UNIQUE_NAME(name) PROF_CONCAT(name, __LINE__)

#define PROF_BEGIN(IDENTIFIER) profilerScopeBegin(IDENTIFIER)

#define PROF_END() profilerScopeEnd()

#define PROF_SCOPE(IDENTIFIER_STR) ProfilerScopedObject PROF_CONCAT(profilerObject, __LINE__)(IDENTIFIER_STR)

#define PROF_SCOPE_FN() ProfilerScopedObject PROF_UNIQUE_NAME(profilerObject)(__FUNCTION__)


