#pragma once

/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/config/defs.hh>
#include <imagine/fs/FSDefs.hh>
#include <imagine/io/ArchiveIO.hh>
#include <imagine/util/string/CStringView.hh>
#include <compare>
#include <memory>

namespace IG::FS
{

class ArchiveIterator : public std::iterator<std::input_iterator_tag, IG::ArchiveEntry>
{
public:
	constexpr ArchiveIterator() = default;
	ArchiveIterator(IG::CStringView path);
	ArchiveIterator(IG::GenericIO io);
	ArchiveIterator(IG::ArchiveEntry entry);
	ArchiveIterator(const ArchiveIterator&) = default;
	ArchiveIterator(ArchiveIterator&&) = default;
	ArchiveIterator &operator=(ArchiveIterator &&o) = default;
	IG::ArchiveEntry& operator*();
	IG::ArchiveEntry* operator->();
	void operator++();
	bool operator==(ArchiveIterator const &rhs) const;
	void rewind();
	bool hasEntry() const { return (bool)impl; }

private:
	std::shared_ptr<IG::ArchiveEntry> impl{};
};

static const ArchiveIterator &begin(const ArchiveIterator &iter)
{
	return iter;
}

static ArchiveIterator end(const ArchiveIterator &)
{
	return {};
}

IG::ArchiveIO fileFromArchive(IG::CStringView archivePath, std::string_view filePath);
IG::ArchiveIO fileFromArchive(IG::GenericIO io, std::string_view filePath);
bool hasArchiveExtension(std::string_view name);

};
