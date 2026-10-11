// framework::ImageLoader implementation - see ImageLoader.h.
#include "ImageLoader.h"

#include <FreeImage.h>

#include <cstdio>
#include <cstring>

namespace framework {

namespace ImageLoader {

namespace {

// FreeImage reads through a FreeImageIO callback table; these adapters run
// that interface over a plain memory span so buffers decode like files.
struct MemoryStream {
	const unsigned char* Data = nullptr;
	std::size_t Size = 0;
	std::size_t Cursor = 0;
};

unsigned DLL_CALLCONV streamRead(void* buffer, unsigned size, unsigned count, fi_handle handle) {
	auto* stream = static_cast<MemoryStream*>(handle);
	if (size == 0)
		return 0;
	const std::size_t requested = static_cast<std::size_t>(size) * count;
	const std::size_t remaining = stream->Size - stream->Cursor;
	const std::size_t copied = requested < remaining ? requested : remaining;
	memcpy(buffer, stream->Data + stream->Cursor, copied);
	stream->Cursor += copied;
	return static_cast<unsigned>(copied / size);
}

int DLL_CALLCONV streamSeek(fi_handle handle, long offset, int origin) {
	auto* stream = static_cast<MemoryStream*>(handle);
	std::size_t base = 0;
	if (origin == SEEK_CUR)
		base = stream->Cursor;
	else if (origin == SEEK_END)
		base = stream->Size;
	const long long target = static_cast<long long>(base) + offset;
	if (target < 0 || target > static_cast<long long>(stream->Size))
		return -1;
	stream->Cursor = static_cast<std::size_t>(target);
	return 0;
}

long DLL_CALLCONV streamTell(fi_handle handle) {
	auto* stream = static_cast<MemoryStream*>(handle);
	return static_cast<long>(stream->Cursor);
}

// Decodes one loaded bitmap into out (32bpp, top-down, RGBA byte order) and
// releases it. Takes ownership: the input is unloaded on every path.
bool decodeToRgba(FIBITMAP* bitmap, DecodedImage& out) {
	if (!bitmap)
		return false;
	// ConvertTo32Bits always returns a fresh bitmap (it clones 32bpp input),
	// so the loaded original is released before the result takes over.
	FIBITMAP* rgba = FreeImage_ConvertTo32Bits(bitmap);
	FreeImage_Unload(bitmap);
	if (!rgba)
		return false;

	const int width = FreeImage_GetWidth(rgba);
	const int height = FreeImage_GetHeight(rgba);
	if (width <= 0 || height <= 0) {
		FreeImage_Unload(rgba);
		return false;
	}

	// FreeImage scans bottom-up BGRA; the output is top-down RGBA.
	FreeImage_FlipVertical(rgba);
	const int stride = width * 4;
	out.Pixels.resize(static_cast<std::size_t>(stride) * height);
	for (int y = 0; y < height; y++) {
		const BYTE* src = FreeImage_GetScanLine(rgba, y);
		unsigned char* dst = out.Pixels.data() + static_cast<std::size_t>(y) * stride;
		for (int x = 0; x < width; x++) {
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}
	FreeImage_Unload(rgba);
	out.Width = width;
	out.Height = height;
	return true;
}

bool loadStream(FreeImageIO& io, fi_handle handle, DecodedImage& out) {
	const FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromHandle(&io, handle, 0);
	if (format == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(format))
		return false;
	// The format sniff left the cursor mid-stream; plugin loaders read from
	// the start.
	streamSeek(handle, 0, SEEK_SET);
	return decodeToRgba(FreeImage_LoadFromHandle(format, &io, handle, 0), out);
}

} // namespace

bool loadFromFile(const char* path, DecodedImage& out) {
	out = DecodedImage();
	if (!path || !*path)
		return false;
	// Signature first, extension only as the fallback for headerless
	// formats (TGA has no magic bytes).
	FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path, 0);
	if (format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilename(path);
	if (format == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(format))
		return false;
	return decodeToRgba(FreeImage_Load(format, path, 0), out);
}

bool loadFromMemory(const unsigned char* data, std::size_t size, DecodedImage& out) {
	out = DecodedImage();
	if (!data || size == 0)
		return false;
	FreeImageIO io = {};
	io.read_proc = streamRead;
	io.seek_proc = streamSeek;
	io.tell_proc = streamTell;
	MemoryStream stream;
	stream.Data = data;
	stream.Size = size;
	return loadStream(io, &stream, out);
}

}

}
