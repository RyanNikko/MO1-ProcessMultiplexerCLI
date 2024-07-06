#include <iostream>
#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <fstream> 
#include <iostream> 
#include <thread>
#include <queue>
#include <map>
#include <conio.h>
#include <condition_variable>
#include <Windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <iomanip>
#include <atomic>
#include <random>


static int MARQUEE_SCREEN = -99;
static int MAIN_SCREEN = 0;

class BufferPrint;
class Process;
class ReadyQueue;
class CoreCPU;
struct SchedulerConfig;
struct SchedulerState;


std::string format_time(const std::chrono::system_clock::time_point& tp) {
	std::time_t now_c = std::chrono::system_clock::to_time_t(tp);

	std::tm now_tm;

#ifdef _WIN32
	if (localtime_s(&now_tm, &now_c) != 0) {
		throw std::runtime_error("Failed to convert time using localtime_s");
	}
#else

	std::tm* local_tm_ptr = std::localtime(&now_c);
	if (!local_tm_ptr) {
		throw std::runtime_error("Failed to convert time using localtime");
	}
	now_tm = *local_tm_ptr;
#endif

	std::stringstream ss;
	ss << std::put_time(&now_tm, "%m/%d/%Y %H:%M:%S");
	return ss.str();
}



class BufferPrint {
private:
	HANDLE hConsole;
	COORD bufferSize;
	SMALL_RECT windowSize;
	std::map<int, std::vector<std::string>> persistentLines;
	std::map<int, std::vector<std::string>> nonPersistentLines;
	std::map<int, std::vector<std::string>> lastRenderedLines;
	std::string currentInput;
	int currentScreen;
	std::mutex mutex;

	std::thread inputThread;
	std::thread renderThread;

	std::queue<char> inputQueue;
	std::mutex inputQueueMutex;
	std::condition_variable inputQueueCV;

	std::atomic<bool> running;

	std::atomic<bool> paused;
	std::condition_variable pauseCV;
	std::mutex pauseMutex;
	//std::atomic<bool> halted;

	void writeLineAt(const std::string& line, SHORT y) {
		COORD pos = { 0, y };
		SetConsoleCursorPosition(hConsole, pos);
		DWORD written;
		WriteConsoleA(hConsole, line.c_str(), static_cast<DWORD>(line.length()), &written, nullptr);

		// Clear the rest of the line
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(hConsole, &csbi);
		DWORD cellsToWrite = csbi.dwSize.X - written;
		FillConsoleOutputCharacterA(hConsole, ' ', cellsToWrite, { static_cast<SHORT>(written), y }, &written);
	}

	void inputThreadFunc() {
		while (running) {
			//while (halted) {
			//	std::this_thread::sleep_for(std::chrono::milliseconds(25));
			//}
			{
				std::unique_lock<std::mutex> lock(pauseMutex);
				pauseCV.wait(lock, [this] { return !paused || !running; });
			}
			if (!running) break;

			std::this_thread::sleep_for(std::chrono::milliseconds(25));
			int ch = _getch();
			if (ch == EOF) continue;

			std::lock_guard<std::mutex> lock(inputQueueMutex);
			inputQueue.push(static_cast<char>(ch));
			inputQueueCV.notify_one();
		}
	}

	void renderThreadFunc() {
		while (running) {
			{
				std::unique_lock<std::mutex> lock(pauseMutex);
				pauseCV.wait(lock, [this] { return !paused || !running; });
			}
			if (!running) break;
			//while (halted) {
			//	std::this_thread::sleep_for(std::chrono::milliseconds(16));
			//}

			{
				std::lock_guard<std::mutex> lock(mutex);
				auto& currentNonPersistentLines = nonPersistentLines[currentScreen];
				auto& currentPersistentLines = persistentLines[currentScreen];
				auto& lastRendered = lastRenderedLines[currentScreen];

				for (size_t i = 0; i < currentNonPersistentLines.size(); ++i) {
					if (i >= lastRendered.size() || currentNonPersistentLines[i] != lastRendered[i]) {
						writeLineAt(currentNonPersistentLines[i], static_cast<SHORT>(i));
					}
				}

				size_t startY = currentNonPersistentLines.size();
				for (size_t i = 0; i < currentPersistentLines.size(); ++i) {
					size_t index = startY + i;
					if (index >= lastRendered.size() || currentPersistentLines[i] != lastRendered[index]) {
						writeLineAt(currentPersistentLines[i], static_cast<SHORT>(index));
					}
				}

				size_t inputY = startY + currentPersistentLines.size();
				if (inputY >= lastRendered.size() || currentInput != lastRendered[inputY]) {
					writeLineAt(currentInput, static_cast<SHORT>(inputY));
				}

				lastRendered = currentNonPersistentLines;
				lastRendered.insert(lastRendered.end(), currentPersistentLines.begin(), currentPersistentLines.end());
				lastRendered.push_back(currentInput);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
		}
	}

public:
	BufferPrint() : currentScreen(0), running(true), paused(false) {
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(hConsole, &csbi);
		bufferSize = csbi.dwSize;
		windowSize = csbi.srWindow;

		inputThread = std::thread(&BufferPrint::inputThreadFunc, this);
		renderThread = std::thread(&BufferPrint::renderThreadFunc, this);
	}

	~BufferPrint() {
		running = false;
		if (inputThread.joinable()) inputThread.join();
		if (renderThread.joinable()) renderThread.join();
	}

	std::string getLineInput(const std::string& prompt = "") {
		std::string input;
		{
			std::lock_guard<std::mutex> lock(mutex);
			currentInput = prompt;
		}

		while (true) {
			std::unique_lock<std::mutex> lock(inputQueueMutex);
			inputQueueCV.wait(lock, [this] { return !inputQueue.empty() || !running; });

			if (!running) break;

			char ch = inputQueue.front();
			inputQueue.pop();
			lock.unlock();

			if (ch == '\r') break;

			if (ch == '\b') {
				if (!input.empty()) {
					input.pop_back();
					std::lock_guard<std::mutex> inputLock(mutex);
					currentInput = prompt + input;
				}
			}
			else {
				input += ch;
				std::lock_guard<std::mutex> inputLock(mutex);
				currentInput = prompt + input;
			}
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			persistentLines[currentScreen].push_back(currentInput);
			currentInput.clear();
		}

		return input;
	}

	void print(const std::string& text, bool isPersistent = false) {
		std::lock_guard<std::mutex> lock(mutex);
		if (isPersistent) {
			persistentLines[currentScreen].push_back(text);
		}
		else {
			nonPersistentLines[currentScreen].push_back(text);
		}
	}
	void switchScreen(int newScreen) {
		std::lock_guard<std::mutex> lock(mutex);
		if (newScreen != currentScreen) {
			currentScreen = newScreen;
			currentInput.clear();
			if (persistentLines.find(currentScreen) == persistentLines.end()) {
				persistentLines[currentScreen] = std::vector<std::string>();
				nonPersistentLines[currentScreen] = std::vector<std::string>();
			}
			lastRenderedLines[currentScreen].clear(); // Clear last rendered lines for the new screen
			SetConsoleCursorPosition(hConsole, { 0, 0 });

			// Force a full redraw of the new screen
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			GetConsoleScreenBufferInfo(hConsole, &csbi);
			DWORD written;
			FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, { 0, 0 }, &written);
		}
	}

	void pause() {
		std::lock_guard<std::mutex> lock(pauseMutex);
		paused = true;
	}

	void resume() {
		{
			std::lock_guard<std::mutex> lock(pauseMutex);
			paused = false;
		}
		pauseCV.notify_all();
	}
};


class Process {
	public:
		int id;
		int print_count;
		int completed_count = 0;
		std::queue<std::string> print_commands;
		std::string latest_time = "";
		std::string name;

		Process(int id, int print_count) : id(id), print_count(print_count) {
			name = "P" + std::to_string(id);
			for (int i = 0; i < print_count; ++i) {
				print_commands.push("Hello world from process " + std::to_string(id) + "!");
			}
		}
};

class ReadyQueue {
private:
	std::mutex mutex;
public:
	std::queue<Process*> process_queue;
	std::map<std::string, Process*> unfinished_processes;
	std::map<int, Process*> running_processes;
	std::vector<Process*> finished_processes;

	void add_to_process_queue(Process* process) {
		std::lock_guard<std::mutex> lock(mutex);
		process_queue.push(process);
		unfinished_processes[process->name] = process;

	}

	void return_to_process_queue(Process* process) {
		std::lock_guard<std::mutex> lock(mutex);
		running_processes.erase(process->id);
		process_queue.push(process);
	}

	//Process* remove_from_process_queue() {
	//	std::lock_guard<std::mutex> lock(mutex);
	//	if (process_queue.empty()) {
	//		return nullptr;
	//	}
	//	Process* process = process_queue.front();
	//	process_queue.pop();
	//	return process;
	//}

	Process* on_process_running() {
		std::lock_guard<std::mutex> lock(mutex);
		if (process_queue.empty()) {
			return nullptr;
		}
		Process* process = process_queue.front();
		process_queue.pop();
		running_processes[process->id] = process;
		return process;
	}

	void on_finish_process(int id) {
		std::lock_guard<std::mutex> lock(mutex);
		auto it = running_processes.find(id);
		if (it != running_processes.end()) {
			Process* process = it->second;
			running_processes.erase(it);
			unfinished_processes.erase(process->name);
			finished_processes.push_back(process);
		}
	}
};

struct SchedulerConfig {
	int numCPU = -1;
	std::string schedulerType = "n/a";
	int quantumCycles = -1;
	bool preemptive = false;
	double batchProcessFreq = -1;
	int minIns = -1;
	int maxIns = -1;
	double delaysPerExec = -1;
};

struct SchedulerState {
	public:
		std::vector<CoreCPU*> cpus;
		bool is_scheduler_test_running = false;
		ReadyQueue ready_queue;
		int generation = 0;
		std::condition_variable cv;
		bool is_screen_s_running = false;
};

class CoreCPU {
public:
	std::mutex mutex;
	std::string name;
	Process* current_process = nullptr;
	//Always on. This method is just processing the different scheduling algorithm.
	void process_realtime(BufferPrint* printer, SchedulerState* state, SchedulerConfig* config) {
		if (config->schedulerType == "fcfs") {
			while (true) { // real time
				current_process = nullptr;
				{
					std::unique_lock<std::mutex> lock(mutex);
					state->cv.wait(lock, [state]() { return !state->ready_queue.process_queue.empty(); });
					current_process = state->ready_queue.on_process_running();
				}
				if (current_process) {
					// Simulate processing the current process
					while (!current_process->print_commands.empty()) {
						std::string command = current_process->print_commands.front();
						//printer->print(this->name + ": " + current_process->name + ": " + std::to_string(current_process->print_commands.size()));
						current_process->print_commands.pop();
						current_process->completed_count++;
						current_process->latest_time = format_time(std::chrono::system_clock::now());
						std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(config->delaysPerExec * 1000)));
					}

					//{
					//std::unique_lock<std::mutex> lock(mutex);
					state->ready_queue.on_finish_process(current_process->id);
					//}
					
				}
			}
		}
		else if (config->schedulerType == "rr") {
			while (true) { // real time
				current_process = nullptr;
				{
					std::unique_lock<std::mutex> lock(mutex);
					state->cv.wait(lock, [state]() { return !state->ready_queue.process_queue.empty(); });
					current_process = state->ready_queue.on_process_running();
				}

				if (current_process) {
					int quantum = config->quantumCycles - 1; //We are similar to do-while loop, we subtract.
					bool process_completed = false;

					//std::cout << this->name << ": " << current_process->name << ": " << current_process->print_commands.size() << std::endl;

					while (true) {
						bool should_continue = false;
						{
							std::lock_guard<std::mutex> lock(mutex);
							if (!current_process->print_commands.empty()) {
								std::string command = current_process->print_commands.front();
								current_process->print_commands.pop();
								current_process->completed_count++;
								should_continue = true;
								//printer->print(this->name + ": " + current_process->name + ": " + std::to_string(current_process->print_commands.size()));
							}
						}
						if (quantum > 0 && should_continue) {
							quantum--;
							
							std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(config->delaysPerExec * 1000)));
						}
						else {
							break;
						}
					}
						
					if (current_process->print_commands.empty()) {
						state->ready_queue.on_finish_process(current_process->id);
					}
					else {
						//std::lock_guard<std::mutex> lock(mutex);
						state->ready_queue.return_to_process_queue(current_process);
					}
					
				}
			}
		}
		
	}
	CoreCPU(std::string name, BufferPrint* p, SchedulerState* s, SchedulerConfig* c): name(name) {
		
		std::thread processing_unit(&CoreCPU::process_realtime, this, p, s, c);
		processing_unit.detach();
	}
};


	 //The "initialize" commands should read from a “config.txt” file, the parameters for CPU scheduler and process attributes.
	 // File format to read in txt:
	 // No. of CPU -> int					 -> int
	 // Scheduler Type,				  -> string /fcfs, sjf, or rr
	 // quantum-cycles (if rr)			 -> int
	 // preemptive (if sjf)				 -> int /1 or 0
	 // batch-process-freq				 -> int	/seconds
	 // min-ins							 -> int /seconds
	 // max-ins							 -> int / seconds
	 // delays-per-exec					 -> int / seconds

SchedulerConfig* initialize() {

	SchedulerConfig* config = new SchedulerConfig();
	std::ifstream file("config.txt");

	if (!file.is_open()) {
		std::cerr << "Error opening config.txt" << std::endl;
		exit(1);
	}

	file >> config->numCPU;
	file >> config->schedulerType;

	file >> config->quantumCycles; // for rr

	//for sjf
	int preemptiveInt;
	file >> preemptiveInt;
	config->preemptive = (preemptiveInt == 1);

	/*if (config->schedulerType == "rr") {
		file >> config->quantumCycles;
	}
	else {
		config->quantumCycles = 0;
	}

	if (config->schedulerType == "sjf") {
		int preemptiveInt;
		file >> preemptiveInt;
		config->preemptive = (preemptiveInt == 1);
	}
	else {
		config->preemptive = false;
	}*/

	file >> config->batchProcessFreq;
	file >> config->minIns;
	file >> config->maxIns;
	file >> config->delaysPerExec;

	file.close();
	return config;
}

void printSchedulerConfig(const SchedulerConfig* config, BufferPrint* bp) {
	if (config == nullptr) {
		bp->print("SchedulerConfig is null");
		return;
	}

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << config->batchProcessFreq;

	bp->print("CSOPESY Simulator");
	bp->print("===================================");
	bp->print("Scheduler Configuration");
	bp->print("Number of CPUs: " + std::to_string(config->numCPU));
	bp->print("Scheduler Type: " + config->schedulerType);
	bp->print("Quantum Cycles: " + std::to_string(config->quantumCycles));
	bp->print("Preemptive: " + std::string(config->preemptive ? "Yes" : "No"));
	bp->print("Batch Process Frequency: " + oss.str() + " seconds");
	bp->print("Minimum Instructions: " + std::to_string(config->minIns));
	bp->print("Maximum Instructions: " + std::to_string(config->maxIns));
	oss.str("");
	oss.clear();
	oss << std::fixed << std::setprecision(3) << config->delaysPerExec;
	bp->print("Delays per Execution: " + oss.str() + " seconds");
	bp->print("===================================");
	bp->print("");
}

//For marquee only:
//================================================================================================
const int FPS = 40;
const int TEST_ITERATION = 10000;

class Utility {
public:
	static std::mutex consoleMutex;

	static void setCursorPosition(SHORT x, SHORT y) {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		COORD coord = { x, y };
		SetConsoleCursorPosition(hConsole, coord);
	}

	static void writeToConsole(SHORT x, SHORT y, const std::string& text) {
		std::lock_guard<std::mutex> lock(consoleMutex);
		setCursorPosition(x, y);
		std::cout << text;
	}

	static void clearLine(int y) {
		std::lock_guard<std::mutex> lock(consoleMutex);
		setCursorPosition(0, y);
		std::cout << "\033[K";
	}
};

std::mutex Utility::consoleMutex;
bool isMarqueeExited = false;

class PersistentText {
public:
	std::string inputted;
	std::vector<std::string> log;
};

class BouncingText {

public:
	BouncingText(SHORT x, SHORT y, std::string print) {
		this->x = x;
		this->y = y;
		this->print = print;
	}

	SHORT x, y;
	SHORT mX = 1, mY = 1;
	std::string print;

	void move() {
		x += mX;
		y += mY;
		Utility::writeToConsole(x, y, print);
	}

	void adjust() {
		HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(console, &csbi);

		int consoleWidth = 98;
		int consoleHeight = 8 + 3 + 6;

		Utility::clearLine(y + 2);
		Utility::clearLine(y + 1);
		Utility::clearLine(y);
		Utility::clearLine(y - 1);
		Utility::clearLine(y - 2);

		if (x >= consoleWidth) {
			mX = -1;
		}
		else if (x <= 0) {
			mX = 1;
		}
		if (y >= consoleHeight) {
			mY = -1;
		}
		else if (y <= 1 + 5) {
			mY = 1;
		}
	}
};


static void displayBouncingTextThreaded(BouncingText& text) {

	const auto targetFrameTime = std::chrono::microseconds(1000000 / FPS);
	int iteration = 0;
	int average = 0;
	while (!isMarqueeExited) {
		//iteration++;
		auto startTime = std::chrono::high_resolution_clock::now();

		text.adjust();
		text.move();

		auto endTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
		/*average += elapsedTime.count();
		if (iteration >= TEST_ITERATION)
			break;*/
		std::this_thread::sleep_for(targetFrameTime - elapsedTime);
	}
	/*average = average / iteration;
	Utility::writeToConsole(0, 70, "Bouncing Thread Time: " + std::to_string(average));*/
	//std::cout << "BouncingThread Time: " << average << std::endl;
}


static void headerThreaded() {

	const auto targetFrameTime = std::chrono::microseconds(1000000 / FPS);
	//int average = 0;
	//int iteration = 0;

	while (!isMarqueeExited) {
		//iteration++;

		auto startTime = std::chrono::high_resolution_clock::now();

		Utility::writeToConsole(0, 0, "****************************************");
		Utility::writeToConsole(0, 1, "   *  Displaying a marquee console! *  ");
		Utility::writeToConsole(0, 2, "****************************************");

		auto endTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
		std::this_thread::sleep_for(targetFrameTime - elapsedTime);
		/*average += elapsedTime.count();
		if (iteration >= TEST_ITERATION)
		   break;*/
	}
	//average = average / iteration;
	/*Utility::writeToConsole(0, 73, "Header Thread Time: " + std::to_string(average));*/

}

static void keyPollThreaded(PersistentText& text) {

	int iteration = 0;
	long long average = 0;
	const auto targetFrameTime = std::chrono::microseconds(1000000 / FPS);

	std::thread displayKeyboardCommand([](PersistentText& text) {
		const auto targetFrameTime = std::chrono::microseconds(1000000 / FPS);
		int average = 0;
		int iteration = 0;
		while (!isMarqueeExited) {
			//iteration++;
			auto startTime = std::chrono::high_resolution_clock::now();
			std::string input = "Enter a command for MARQUEE_CONSOLE: " + text.inputted;

			int baseY = 3 + 6 + 8 + 1 + 6;
			Utility::clearLine(baseY);
			Utility::writeToConsole(0, baseY, input);

			for (int size = 0; size < text.log.size(); size++) {
				Utility::clearLine(size + baseY + 1);
				std::string logged_text = "Command processed in MARQUEE_CONSOLE: " + text.log[size];
				Utility::writeToConsole(0, size + baseY + 1, logged_text);

			}

			Utility::setCursorPosition(input.length(), baseY);
			auto endTime = std::chrono::high_resolution_clock::now();
			auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
			/*average += elapsedTime.count();
			if (iteration >= TEST_ITERATION)
				break;*/
			std::this_thread::sleep_for(targetFrameTime - elapsedTime);
		}


		/*average = average / iteration;
		Utility::writeToConsole(0, 76, "Display Poll Thread Time: " + std::to_string(average));*/
		}, std::ref(text));

	//char key = '\0';

	while (!isMarqueeExited) {
		/*iteration++;
		auto startTime = std::chrono::high_resolution_clock::now();*/
		//Utility::writeToConsole(0, 150, "Key is: " + std::to_string(key));
		if (_kbhit()) {
			char key = _getch();

			

			if (key == '\r') {  // Enter key
				if (text.inputted == "exit") {
					isMarqueeExited = true;
					break;
				}
				else {
					text.log.push_back(text.inputted);
					text.inputted = std::string();  // Clear the input string for the next line
				}
			}
			else if (key == '\b') {  // Backspace key
				if (!text.inputted.empty()) {
					text.inputted.pop_back();  // Remove the last character from the string
				}
			}
			else {
				text.inputted += key;  // Concatenate the character to the input string
			}
		}

		/*auto endTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
		average += elapsedTime.count();
		if (iteration >= TEST_ITERATION)
			break;*/

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	/* average = average / iteration;
	 Utility::writeToConsole(0, 79, "Key Poll Thread Time: " + std::to_string(average));*/
	displayKeyboardCommand.join();
}

void marquee() {
	isMarqueeExited = false;

	BouncingText bouncingText(0, 3, "This is a marquee text");
	PersistentText persistentText;

	std::thread headerThread(headerThreaded);
	std::thread bouncingTextThread([](BouncingText& b) {displayBouncingTextThreaded(b); }, std::ref(bouncingText));
	std::thread persistentTextThread([](PersistentText& p) {  keyPollThreaded(p); }, std::ref(persistentText));

	headerThread.join();
	bouncingTextThread.join();
	persistentTextThread.join();


}
//Marquee End
//================================================================================================


int main() {
	BufferPrint* buffered_printer = new BufferPrint();
	SchedulerConfig* config = nullptr;
	SchedulerState* state = nullptr;

	std::unordered_map<std::string, std::function<void(SchedulerConfig* config, SchedulerState* state, BufferPrint* bp)>> commands;

	auto command_screen_r = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp, std::string command) {
		Process* process = state->ready_queue.unfinished_processes[command];

		bp->switchScreen(process->id + 1);

		std::string screenCommand;
		while (true) {
			bp->print("Process: " + process->name);
			bp->print("Id: " + std::to_string(process->id));
			bp->print("Process print count: " + std::to_string(process->completed_count) + "/" + std::to_string(process->print_count));
			//bp->print("Enter a command: ");
			screenCommand = bp->getLineInput("Enter a command: ");
			//std::getline(std::cin, screenCommand);

			if (screenCommand == "exit") {
				bp->switchScreen(MAIN_SCREEN);
				break;  // Exit the screen and return to main menu
			}
			else if (screenCommand == "process-smi") {
				bp->print("");
				bp->print("");
				continue;
			}
			else {
				bp->print("Invalid command", true);
			}
		}

		//bp->clearScreen();
		};

	commands["screen-ls"] = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
		if (!config || !state || !bp)
		{
			bp->print("Something has gone wrong with config and/or state and/or bp");
			return;
		}
			
		bp->print("");
		bp->print("");


		int cpu_running_counter = 0;

		for (const auto cpu : state->cpus) {
			std::lock_guard<std::mutex> lock(cpu->mutex);
			if (cpu->current_process != nullptr) {
				cpu_running_counter++;
			}
		}

		double perc = static_cast<double>(cpu_running_counter) / config->numCPU;
		std::stringstream ss;
		ss << std::fixed << std::setprecision(2) << (perc * 100) << "%";
		bp->print("CPU Utilization: " + ss.str());
		bp->print("Cores Used: " + std::to_string(cpu_running_counter));
		bp->print("Cores Available: " + std::to_string(config->numCPU - cpu_running_counter));
		bp->print("");
		bp->print("---------------------------------------------");
		bp->print("Running processes:");
		for (const auto cpu : state->cpus) {		
			std::unique_lock<std::mutex> lock(cpu->mutex);
			if (cpu->current_process != nullptr) {
				std::string time = cpu->current_process->latest_time;			
				bp->print(cpu->current_process->name + "     " + time + "     Core: " + cpu->name + " " + std::to_string(cpu->current_process->completed_count) + " / " + std::to_string(cpu->current_process->print_count));		
			}	
			lock.unlock();
		}

		bp->print("");
		bp->print("Finished processes:");

		for (const auto processes : state->ready_queue.finished_processes) {
			std::string time = processes->latest_time;
			bp->print(processes->name + "     " + time + "     Finished " + std::to_string(processes->completed_count) + " / " + std::to_string(processes->print_count));
		}
		bp->print("---------------------------------------------");
		bp->print("");
		bp->print("");
	};

	commands["marquee"] = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
		bp->pause();
		// Force a full redraw of the new screen
		/*auto clear = []() {
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
			DWORD written;
			FillConsoleOutputCharacter(GetStdHandle(STD_OUTPUT_HANDLE), ' ', csbi.dwSize.X * csbi.dwSize.Y, { 0, 0 }, &written);
		};
		clear();*/
		bp->switchScreen(MARQUEE_SCREEN);
		marquee();
		bp->resume();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		bp->switchScreen(MAIN_SCREEN);	
	};

	commands["scheduler-test"] = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
		if (!config || !state)
			return;

		if (state->is_scheduler_test_running) {
			bp->print("Scheduler-Test is already ongoing...", true);
			return;
		}

		state->is_scheduler_test_running = true;
		
		while (state->is_scheduler_test_running) {
			std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(config->batchProcessFreq * 1000)));
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<> dis(config->minIns, config->maxIns);

			Process* process = new Process(state->generation++, dis(gen));
			state->ready_queue.add_to_process_queue(process);
			state->cv.notify_one();
		}
		};

	commands["scheduler-stop"] = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
		if (state) {
			if (state->is_scheduler_test_running) {
				state->is_scheduler_test_running = false;
			}
			else {
				bp->print("Scheduler-Test is already stopped...", true);
			}
		}

		};

	commands["report-util"] = [](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
		if (!config || !state || !bp)
		{
			// Since we're not using bp->print anymore, we'll write to a file even in this error case
			std::ofstream outFile("csopesy-log.txt", std::ios::app);
			outFile << "Something has gone wrong with config and/or state and/or bp\n";
			outFile.close();
			return;
		}

		std::ofstream outFile("csopesy-log.txt", std::ios::app);

		int cpu_running_counter = 0;
		for (const auto cpu : state->cpus) {
			std::lock_guard<std::mutex> lock(cpu->mutex);
			if (cpu->current_process != nullptr) {
				cpu_running_counter++;
			}
		}
		double perc = static_cast<double>(cpu_running_counter) / config->numCPU;
		std::stringstream ss;
		ss << std::fixed << std::setprecision(2) << (perc * 100) << "%";
		outFile << "CPU Utilization: " << ss.str() << "\n";
		outFile << "Cores Used: " << cpu_running_counter << "\n";
		outFile << "Cores Available: " << (config->numCPU - cpu_running_counter) << "\n";
		outFile << "\n";
		outFile << "---------------------------------------------\n";
		outFile << "Running processes:\n";
		for (const auto cpu : state->cpus) {
			std::unique_lock<std::mutex> lock(cpu->mutex);
			if (cpu->current_process != nullptr) {
				std::string time = cpu->current_process->latest_time;
				outFile << cpu->current_process->name << "     " << time << "     Core: " << cpu->name << " "
					<< cpu->current_process->completed_count << " / " << cpu->current_process->print_count << "\n";
			}
			lock.unlock();
		}
		outFile << "\n";
		outFile << "Finished processes:\n";
		for (const auto processes : state->ready_queue.finished_processes) {
			std::string time = processes->latest_time;
			outFile << processes->name << "     " << time << "     Finished "
				<< processes->completed_count << " / " << processes->print_count << "\n";
		}
		outFile << "---------------------------------------------\n";
		outFile << "\n\n";

		outFile.close();
		};

	std::string command = "";

	while (true) {
		//buffered_printer->print("Enter initialize to start: ", true);
		command = buffered_printer->getLineInput("Enter initialize to start: ");
		//std::getline(std::cin, command);
		if (command == "exit") {
			buffered_printer->print("Exiting program...", true);
			return 0;
		}
		else if (command != "initialize") {
			buffered_printer->print("Command unrecognized. Please try again", true);
		}
		else {
			config = initialize();
			state = new SchedulerState();

			for (int i = 0; i < config->numCPU; i++) {
				CoreCPU* cpu = new CoreCPU("CPU_" + std::to_string(i), buffered_printer,  state, config);
				state->cpus.push_back(cpu);
			}

			break;
		}
	}

	printSchedulerConfig(config, buffered_printer);

	if (!state || !config) {
		buffered_printer->print("Program unable to initialize state and/or config!", true);
		return -2;
	}

	while (command != "exit") {
		//Locks the main menu from reading while screen -s x is active
		while (state->is_screen_s_running) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		command = "";
		//buffered_printer->print("Enter a command: ", true);
		//std::getline(std::cin, command);
		command = buffered_printer->getLineInput("Enter a command: ");

		//if (command.find("make8") != std::string::npos) {
		//	for (int i = 0; i < 8; i++) {
		//		Process* process = new Process(state->generation++, 14);
		//		state->ready_queue.add_to_process_queue(process);
		//		state->cv.notify_one();
		//	}	
		//}

		if (command.find("screen -s ") != std::string::npos) {
			command = command.substr(10, command.size());
			if (state->ready_queue.unfinished_processes.count(command) > 0) {
				buffered_printer->print("Process already exists!", true);
				continue;
			}
			else {
				Process* process = new Process(state->generation++, rand() % (config->maxIns - config->minIns) + config->minIns + 1);
				process->name = command;
				state->ready_queue.add_to_process_queue(process);
				state->cv.notify_one();
				command = "screen -r " + command; //Makes it so that screen -r is executed too.
			}

		}

		if (command.find("screen -r ") != std::string::npos) {
			command = command.substr(10, command.size());
			if (state->ready_queue.unfinished_processes.count(command) > 0) {
				command_screen_r(config, state, buffered_printer, command);
			}
			else {
				buffered_printer->print("Process " + command + " not found.", true);
			}

		}
		else if (commands.find(command) == commands.end()) {
			buffered_printer->print("Invalid command.", true);
		}
		else {
			auto function = commands[command];
			std::thread execute([function](SchedulerConfig* config, SchedulerState* state, BufferPrint* bp) {
				function(config, state, bp);
				}, config, state, buffered_printer);
			execute.detach(); //Make a clean up later...
		}
	}

	buffered_printer->print("Exiting program...", true);

	delete config;
	delete state;
	delete buffered_printer;

	return 0;
}

//int main() {
//	BufferPrint bp;
//	
//	int counter = 0;
//
//	while (counter < 20) {
//		counter++;
//		bp.print(std::to_string(counter) + " => Count", true);
//		bp.print(std::to_string(counter) + " => Count, Non persistent");
//
//		if (counter < 5) {
//			std::this_thread::sleep_for(std::chrono::milliseconds(500));
//			bp.pause();
//			bp.print("Testing xxxaxaxaxa");
//			system("cls");
//			std::this_thread::sleep_for(std::chrono::milliseconds(500));
//			system("cls");
//			SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), { 0,0 });
//			bp.resume();
//		}
//
//		else {
//			std::this_thread::sleep_for(std::chrono::milliseconds(100));
//		}
//
//	}
//
//	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
//
//	bp.switchScreen(1);
//
//	bp.print("This is on a new screen");
//
//	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
//	
//	bp.switchScreen(0);
//
//	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
//
//	bp.switchScreen(1);
//
//	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
//
//	return 0;
//}

