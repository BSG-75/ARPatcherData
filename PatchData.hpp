#include <filesystem>
#include <istream>
#include <string_view>
#include <vector>
#include "Escape.hpp"

struct PatchData {
	int version;
	std::filesystem::path oldFileName;
	std::filesystem::path newFileName;
	EscapeData escapeData;
	std::vector<DataChunk> dataChunks;
};

constexpr auto latestPatchDataVersion = 1000;
constexpr auto patchFileHeader = ([]
{
    using namespace std::string_view_literals; 
    return "红警3吧装甲冲击更新描述文件"sv;
})();
constexpr auto delimiter = std::string_view{"\r\n"};

/*
	Patch File structure:
	[plain text] utf8 patchFileHeader;
	[plain text] latestPatchDataVersion
	\r\n
	(Other contents)

	Patch File Version 1000 structure:

	[plain text] patchFileHeader;
	[plain text] latestPatchDataVersion
	\r\n
	[plain text] oldFileName.length()
	\r\n
	[plain text] utf8 oldFileName
	\r\n
	[plain text] newFileName.length()
	\r\n
	[plain text] utf8 newFileName
	\r\n
	[plain text] numerical value of escaped char
	\r\n
	[plain text] numerical value of substitue char
	\r\n
	[plain text] numerical value of escape char
	\r\n
	[plain text] numerical value of escape2 char
	\r\n
	[plain text] DataChunks array length
	DataChunks[DataChunks array length]

	Layout of DataChunk:
	char[4] chunk length (32bit) in little endian
	char[4] source position (32bit) in little endian
#if soucePosition == -1
    bytes[chunk length]
#endif

*/

template<typename OStream>
void writeChunks(OStream&& out, const PatchData& patchData)
{
	constexpr auto supportedVersion = 1000;
	if (patchData.version != supportedVersion)
	{
		throw std::invalid_argument{ "Unsupported patch data version!" };
	}

	out.exceptions(out.exceptions() | out.badbit | out.failbit);
	out << std::noskipws;

	out.write(patchFileHeader.data(), patchFileHeader.size());
	out << patchData.version;
	out.write(delimiter.data(), delimiter.size());

	auto utf8OldFileName = patchData.oldFileName.u8string();
	out << utf8OldFileName.size();
	out.write(delimiter.data(), delimiter.size());
	out.write(utf8OldFileName.data(), utf8OldFileName.size());
	out.write(delimiter.data(), delimiter.size());

	auto utf8NewFileName = patchData.newFileName.u8string();
	out << utf8NewFileName.size();
	out.write(delimiter.data(), delimiter.size());
	out.write(utf8NewFileName.data(), utf8NewFileName.size());
	out.write(delimiter.data(), delimiter.size());

	out << +patchData.escapeData.toBeEscaped;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.substituteCharacter;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.escape;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.escape2;
	out.write(delimiter.data(), delimiter.size());

	out << patchData.dataChunks.size();
	out.write(delimiter.data(), delimiter.size());
	for (const auto& chunk : patchData.dataChunks)
	{
		writeLittleEndianUInt32(out, chunk.length);
		writeLittleEndianUInt32(out, chunk.sourcePosition);
		if (chunk.sourcePosition == static_cast<decltype(chunk.sourcePosition)>(-1))
		{
			out.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.length);
		}
	}
}

template<typename IStream>
PatchData readChunks(IStream&& in)
{
	constexpr auto supportedVersion = 1000;
	in.exceptions(in.exceptions() | in.badbit | in.failbit);
	in >> std::noskipws;

	auto patchData = PatchData{};
	auto checkHeader = [](std::istream& in, std::string_view check) {
		for (auto character : check)
		{
			if (static_cast<char>(in.get()) != character)
			{
				throw std::invalid_argument{ "Required patch file header not found!" };
			}
		}
	};
	checkHeader(in, patchFileHeader);
	in >> patchData.version;
	if (patchData.version != supportedVersion)
	{
		throw std::invalid_argument{ "Unsupported patch data version! You may need to get a newer version of this program." };
	}
	checkHeader(in, delimiter);

	auto readSizeType = [](std::istream& in) {
		auto result = std::size_t{};
		in >> result;
		return result;
	};


	auto oldFileNameString = std::string{ readSizeType(in),{}, std::string::allocator_type{} };
	checkHeader(in, delimiter);
	in.read(oldFileNameString.data(), oldFileNameString.size());
	patchData.oldFileName = std::filesystem::u8path(oldFileNameString);
	checkHeader(in, delimiter);
	
	auto newFileNameString = std::string{ readSizeType(in),{}, std::string::allocator_type{} };
	checkHeader(in, delimiter);
	in.read(newFileNameString.data(), newFileNameString.size());
	patchData.newFileName = std::filesystem::u8path(newFileNameString);
	checkHeader(in, delimiter);

	auto readUnsignedInt8Bit = [](std::istream& in) {
		auto result = unsigned{};
		in >> result;
		if (result > std::numeric_limits<std::uint8_t>::max())
		{
			throw std::domain_error{ "Input value too large" };
		}
		return static_cast<std::uint8_t>(result);
	};

	patchData.escapeData.toBeEscaped = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.substituteCharacter = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.escape = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.escape2 = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);

	patchData.dataChunks.resize(readSizeType(in));
	checkHeader(in, delimiter);
	for (auto& chunk : patchData.dataChunks)
	{
		chunk.length = readLittleEndianUInt32(in);
		chunk.sourcePosition = readLittleEndianUInt32(in);
		if (chunk.sourcePosition == static_cast<decltype(chunk.sourcePosition)>(-1))
		{
			chunk.data.resize(chunk.length);
			in.read(reinterpret_cast<char*>(chunk.data.data()), chunk.length);
		}
	}

	return patchData;
}