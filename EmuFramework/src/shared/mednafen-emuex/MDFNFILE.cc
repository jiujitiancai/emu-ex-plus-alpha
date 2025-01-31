/*  This file is part of EmuFramework.

	EmuFramework is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	EmuFramework is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "MDFNFILE"
#include <imagine/io/FileIO.hh>
#include <imagine/fs/ArchiveFS.hh>
#include <imagine/util/string.h>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/logger/logger.h>
#include <mednafen/types.h>
#include <mednafen/git.h>
#include <mednafen/file.h>
#include <mednafen/memory.h>
#include <mednafen/MemoryStream.h>
#include <fcntl.h>

namespace EmuEx
{
extern IG::ApplicationContext appCtx;
}

namespace Mednafen
{

static bool hasKnownExtension(std::string_view name, const std::vector<FileExtensionSpecStruct>& extSpec)
{
	for(const auto &e : extSpec)
	{
		if(name.ends_with(e.extension))
			return true;
	}
	return false;
}

MDFNFILE::MDFNFILE(VirtualFS* vfs, const char* path, const std::vector<FileExtensionSpecStruct>& known_ext, const char* purpose):
	ext(f_ext), fbase(f_fbase), f_vfs{vfs}
{
	if(IG::FS::hasArchiveExtension(path))
	{
		try
		{
			for(auto &entry : IG::FS::ArchiveIterator{EmuEx::appCtx.openFileUri(path)})
			{
				if(entry.type() == IG::FS::file_type::directory)
				{
					continue;
				}
				if(hasKnownExtension(entry.name(), known_ext))
				{
					logMsg("archive file entry:%s", entry.name().data());
					auto io = entry.moveIO();
					str = std::make_unique<MemoryStream>(io.size(), true);
					if(io.read(str->map(), str->map_size()) != (int)str->map_size())
					{
						throw MDFN_Error(0, "Error reading archive");
					}
					auto extStr = strrchr(path, '.');
					f_ext = extStr ? extStr + 1 : "";
					return; // success
				}
			}
			throw MDFN_Error(0, "No recognized file extensions in archive");
		}
		catch(...)
		{
			throw MDFN_Error(0, "Error opening archive");
		}
	}
	else
	{
		str.reset(vfs->open(path, VirtualFS::MODE_READ));
		auto extStr = strrchr(path, '.');
		f_ext = extStr ? extStr + 1 : "";
	}
}

MDFNFILE::MDFNFILE(VirtualFS* vfs, std::unique_ptr<Stream> str, const char *path, const char *purpose):
	ext(f_ext), fbase(f_fbase),
	str{std::move(str)},
	f_vfs{vfs}
{
	auto extStr = strrchr(path, '.');
	f_ext = extStr ? extStr + 1 : "";
}

extern int openFdHelper(const char *file, int oflag, mode_t mode)
{
	unsigned openFlags = (oflag & O_CREAT) ? IG::IO::OPEN_CREATE : 0;
	return EmuEx::appCtx.openFileUriFd(file, openFlags | IG::IO::OPEN_TEST).release();
}

}
