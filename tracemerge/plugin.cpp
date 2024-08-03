#include <filesystem>
#include <string>
#include <ranges>
#include "plugin.h"
#include "rsjparser.tcc"

#if defined(_WIN64)
#define PLUGIN_ARCHITECTURE	"x64"
#define TRACEFILE_EXTENSION ".trace64"
#define POINTER_SIZE 8
#else
#define PLUGIN_ARCHITECTURE	"x86"
#define TRACEFILE_EXTENSION ".trace32"
#define POINTER_SIZE 4
#endif

#define MAGIC_SIZE		5
#define OPCODE_SIZE		0b1111
#define THREAD_ID_FLAG	(1 << 7)
#define MERGE_DIRECTORY	".\\db"

namespace fs = std::filesystem;

struct Tracefile
{
	struct Tracedata
	{
		uint8_t registerTransformations{};
		uint8_t memoryAccesses{};
		uint8_t flags_opcodeSize{}; // flags = high nibble, opcode size = low nibble
		uint8_t opcodeSize{};
		uint32_t threadId{};
		uint8_t opcodes[OPCODE_SIZE]{};
		std::vector<uint8_t> vecRegisterTransformations;
		std::vector<uint64_t> vecRegisterTransformationsNewData;
		std::vector<uint8_t> vecMemoryAccessFlags;
		std::vector<uint64_t> vecMemoryAccessAddresses;
		std::vector<uint64_t> vecMemoryAccessOldData;
		std::vector<uint64_t> vecMemoryAccessNewData;
	};

	uint8_t magic[MAGIC_SIZE]{};
	uint32_t jsonSize{};
	RSJresource jsonData{};
	std::vector<Tracedata> vecTracedata;
	std::string hash{};

	Tracefile(fs::directory_entry entry)
	{
		if (!fs::is_regular_file(entry.path()))
			throw std::runtime_error("Path provided is not a regular path!");

		std::ifstream tracefile(entry.path(), std::ios::binary);

		if (!tracefile.is_open())
			throw std::runtime_error("Failed to open file!");

		tracefile.read((char*)&magic, MAGIC_SIZE - 1);
		if (strncmp((char*)&magic, "TRAC", MAGIC_SIZE - 1))
			throw std::runtime_error("Header mismatch!");

		tracefile.read((char*)&jsonSize, sizeof(jsonSize));
		if (!jsonSize || jsonSize > 0x4000)
			throw std::runtime_error("Json size is corrupted!");

		std::string jsonString(jsonSize, '\0');
		tracefile.read(&jsonString.front(), jsonSize);
		jsonData = RSJresource(jsonString);

		if (jsonData["arch"].as<std::string>() != PLUGIN_ARCHITECTURE)
			throw std::runtime_error("Trace file does not match tracemerge plugin architecture!");

		hash = jsonData["hash"].as<std::string>();

		uint8_t pad;
		while (tracefile.read((char*)&pad, sizeof(pad)) && pad == 0)
		{
			Tracedata data{};

			tracefile.read((char*)&data.registerTransformations, sizeof(data.registerTransformations));
			tracefile.read((char*)&data.memoryAccesses, sizeof(data.memoryAccesses));
			tracefile.read((char*)&data.flags_opcodeSize, sizeof(data.flags_opcodeSize));

			data.opcodeSize = data.flags_opcodeSize & OPCODE_SIZE;

			if (data.flags_opcodeSize & THREAD_ID_FLAG)
				tracefile.read((char*)&data.threadId, sizeof(data.threadId));

			tracefile.read((char*)&data.opcodes, data.opcodeSize);

			uint8_t tmpByte{};
			int64_t tmpPtr{};

			for (uint32_t i = 0; i < data.registerTransformations; i++)
			{
				tracefile.read((char*)&tmpByte, sizeof(tmpByte));
				data.vecRegisterTransformations.push_back(tmpByte);
			}

			for (uint32_t i = 0; i < data.registerTransformations; i++)
			{
				tracefile.read((char*)&tmpPtr, POINTER_SIZE);
				data.vecRegisterTransformationsNewData.push_back(tmpPtr);
			}

			for (uint32_t i = 0; i < data.memoryAccesses; i++)
			{
				tracefile.read((char*)&tmpByte, sizeof(tmpByte));
				data.vecMemoryAccessFlags.push_back(tmpByte);
			}

			for (uint32_t i = 0; i < data.memoryAccesses; i++)
			{
				tracefile.read((char*)&tmpPtr, POINTER_SIZE);
				data.vecMemoryAccessAddresses.push_back(tmpPtr);
			}

			for (uint32_t i = 0; i < data.memoryAccesses; i++)
			{
				tracefile.read((char*)&tmpPtr, POINTER_SIZE);
				data.vecMemoryAccessOldData.push_back(tmpPtr);
			}

			for (uint32_t i = 0; i < data.memoryAccesses; i++)
			{
				if (!(data.vecMemoryAccessFlags[i] & 1))
				{
					tracefile.read((char*)&tmpPtr, POINTER_SIZE);
					data.vecMemoryAccessNewData.push_back(tmpPtr);
				}
			}
			vecTracedata.push_back(data);
		}
	}
};

std::string Timestamp()
{
	tm timeptr{};
	time_t temp{};
	std::string timeStr(32, '\0');

	temp = time(0);
	localtime_s(&timeptr, &temp);
	strftime(&timeStr.front(), timeStr.size(), "-%Y%m%d-%H%M%S", &timeptr);

	return timeStr;
}

std::vector<Tracefile> ParseTracefiles(std::vector<std::string_view> vecInputNames, fs::path inputDirectory = MERGE_DIRECTORY) noexcept(false)
{
	fs::directory_entry entry(inputDirectory);
	if (!fs::exists(entry) || !fs::is_directory(entry))
		throw std::runtime_error("Merge directory does not exist or is not a directory!\n");

	std::vector<Tracefile> vecTracefiles;

	for (const auto& file : fs::directory_iterator(entry))
	{
		if (!file.is_regular_file())
			continue;

		for (auto name : vecInputNames)
		{
			if (file.path().filename().string().find(name) != std::string_view::npos)
			{
				dprintf("Parsing %s...\n", std::string(name).c_str());
				vecTracefiles.push_back(Tracefile{ file });

				if (vecInputNames.size() == vecTracefiles.size())
					break;
			}
		}
	}

	if (vecTracefiles.empty())
		throw std::runtime_error("The provided trace files does not exist in the db directory!");

	if (vecInputNames.size() != vecTracefiles.size())
		throw std::runtime_error("At least one trace file is missing from the merge directory!");

	return vecTracefiles;
}

void SaveTracefile(Tracefile& outputTrace, std::string_view outputName, fs::path outputDirectory = MERGE_DIRECTORY) noexcept(false)
{
	fs::directory_entry entry(outputDirectory);
	if (!fs::exists(entry) || !fs::is_directory(entry))
		throw std::runtime_error("Output directory does not exist or is not a directory!\n");

	entry.assign(outputDirectory / outputName);

	std::ofstream outputFile(entry.path(), std::ios::binary);

	outputFile.write((char*)&outputTrace.magic, MAGIC_SIZE - 1);
	outputFile.write((char*)&outputTrace.jsonSize, sizeof(outputTrace.jsonSize));
	outputFile.write(outputTrace.jsonData.raw_data().c_str(), outputTrace.jsonSize);

	uint8_t empty{};
	for (auto& tracedata : outputTrace.vecTracedata)
	{
		// null byte between every trace data
		outputFile.write((char*)&empty, sizeof(empty));

		outputFile.write((char*)&tracedata.registerTransformations, sizeof(tracedata.registerTransformations));
		outputFile.write((char*)&tracedata.memoryAccesses, sizeof(tracedata.memoryAccesses));
		outputFile.write((char*)&tracedata.flags_opcodeSize, sizeof(tracedata.flags_opcodeSize));

		if (tracedata.flags_opcodeSize & THREAD_ID_FLAG)
			outputFile.write((char*)&tracedata.threadId, sizeof(tracedata.threadId));

		outputFile.write((char*)&tracedata.opcodes, tracedata.opcodeSize);

		for (auto& registerTransformation : tracedata.vecRegisterTransformations)
			outputFile.write((char*)&registerTransformation, sizeof(registerTransformation));

		for (auto& registerTransformationNewData : tracedata.vecRegisterTransformationsNewData)
			outputFile.write((char*)&registerTransformationNewData, POINTER_SIZE);

		for (auto& memoryAccess : tracedata.vecMemoryAccessFlags)
			outputFile.write((char*)&memoryAccess, sizeof(memoryAccess));

		for (auto& memoryAccessAddress : tracedata.vecMemoryAccessAddresses)
			outputFile.write((char*)&memoryAccessAddress, POINTER_SIZE);

		for (auto& memoryAccessOldData : tracedata.vecMemoryAccessOldData)
			outputFile.write((char*)&memoryAccessOldData, POINTER_SIZE);

		for (auto& memoryAccessNewData : tracedata.vecMemoryAccessNewData)
			outputFile.write((char*)&memoryAccessNewData, POINTER_SIZE);
	}
}

bool cbTracemerge(int argc, char** argv)
{
	if (argc == 1)
	{
		dprintf("Choose at least 2 trace files from your %s directory.\n", MERGE_DIRECTORY);
		dputs("Usage example: tracemerge tracefile1, tracefile2, tracefile3");
		dputs("This is going to generate a tracefile with tracefile1 header and trace data + tracefile2 trace data + tracefile3 trace data.");
		return false;
	}

	if (argc <= 2)
	{
		dputs("At least 2 files should be selected to merge!");
		dputs("Usage example: tracemerge tracefile1, tracefile2");
		return false;
	}

	dprintf("Starting to merge trace files - output directory: %s\n", MERGE_DIRECTORY);

	try
	{
		std::vector<std::string_view> vecNames;

		for (size_t i = 1; i < argc; i++)
			vecNames.push_back(argv[i]);

		auto vecTracefiles = ParseTracefiles(vecNames);

		Tracefile outputTrace = vecTracefiles.front();

		for (auto& trace : vecTracefiles | std::views::drop(1))
		{
			if ((outputTrace.hash) != trace.hash)
				throw std::runtime_error("Trace files hash mismatch! Probably trace files from different binaries...");

			for (auto& tracedata : trace.vecTracedata)
				outputTrace.vecTracedata.push_back(tracedata);
		}

		std::stringstream outputName;
		outputName << "output_merge" << Timestamp().c_str() << TRACEFILE_EXTENSION;

		SaveTracefile(outputTrace, outputName.str());
		dprintf("Merged successfully! output file: %s\n", outputName.str().c_str());
	}
	catch (std::runtime_error& err)
	{
		dprintf("Could not merge: %s\n", err.what());
		return false;
	}

	return true;
}

bool cbTracedelete(int argc, char** argv)
{
	if (argc == 1)
	{
		dputs("Use one trace file and N number of indexes to be deleted (in hexadecimal)");
		dputs("Usage example: tracedelete tracefile1, 0x539, 5AF, 1003A (0x prefix is optional)");
		dputs("This is going to delete the trace data entries at the 0x539, 0x5AF and 1003A indexes.");
		return false;
	}

	if (argc == 2)
	{
		dputs("Wrong number of arguments! use one tracefile and at least one index in hexadecimal.");
		return false;
	}

	dprintf("Starting to delete trace file indexes - output directory: %s\n", MERGE_DIRECTORY);

	try
	{
		std::string_view traceName(argv[1]);
		std::vector<uint32_t> indexes;

		for (size_t i = 2; i < argc; i++)
			indexes.push_back(strncmp(argv[i], "0x", 2) == 0 ? strtoull(argv[2], NULL, 0) : strtoull(argv[2], NULL, 16));

		Tracefile outputTrace = ParseTracefiles({ traceName }).front();

		std::stringstream ss;

		for (auto& index : indexes)
		{
			if (index >= 0 && index < outputTrace.vecTracedata.size())
			{
				outputTrace.vecTracedata.erase(outputTrace.vecTracedata.begin() + index);
			}
			else
			{
				ss << "Index " << std::hex << index << " is out of bounds for the provided trace file!";
				throw std::runtime_error(ss.str());
			}
		}

		std::stringstream outputName;
		outputName << "output_delete" << Timestamp().c_str() << TRACEFILE_EXTENSION;

		SaveTracefile(outputTrace, outputName.str());
		dprintf("Deleted successfully! output file: %s\n", outputName.str().c_str());
	}
	catch (std::runtime_error& err)
	{
		dprintf("Could not delete: %s\n", err.what());
		return false;
	}
	return true;
}

bool pluginInit(PLUG_INITSTRUCT* initStruct)
{
	if (!_plugin_registercommand(pluginHandle, "tracemerge", cbTracemerge, false))
	{
		dputs("Error registering the \"" "tracemerge" "\" command!");
		return false;
	}

	if (!_plugin_registercommand(pluginHandle, "tracedelete", cbTracedelete, false))
	{
		dputs("Error registering the \"" "tracedelete" "\" command!");
		return false;
	}
	return true;
}

void pluginStop()
{

}

void pluginSetup()
{

}