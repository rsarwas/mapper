/*
 *    Copyright 2012, 2013 Pete Curtis
 *    Copyright 2013, 2015, 2016 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "file_format_registry.h"

#include <QFileInfo>

#include "fileformats/file_format.h"


FileFormatRegistry FileFormats;


// ### FormatRegistry ###

FileFormatRegistry::FileFormatRegistry() noexcept
: default_format_id{ nullptr }
{
	// nothing else
}


FileFormatRegistry::~FileFormatRegistry()
{
	for (auto format : fmts)
		delete format;
}



void FileFormatRegistry::registerFormat(FileFormat *format)
{
	fmts.push_back(format);
	if (fmts.size() == 1) default_format_id = format->id();
	Q_ASSERT(findFormatForFilename(QLatin1String("filename.") + format->primaryExtension()) != nullptr); // There may be more than one format!
	Q_ASSERT(findFormatByFilter(format->filter()) == format); // The filter shall be unique at least by description.
}

std::unique_ptr<FileFormat> FileFormatRegistry::unregisterFormat(const FileFormat* format)
{
	std::unique_ptr<FileFormat> ret;
	auto it = std::find(begin(fmts), end(fmts), format);
	if (it != end(fmts))
	{
		ret.reset(*it);
		fmts.erase(it);
	}
	return ret;
}

const FileFormat *FileFormatRegistry::findFormat(const char* id) const
{
	for (auto format : fmts)
	{
		if (qstrcmp(format->id(), id) == 0) return format;
	}
	return nullptr;
}

const FileFormat *FileFormatRegistry::findFormatByFilter(const QString& filter) const
{
	for (auto format : fmts)
	{
		// Compare only before closing ')'. Needed for QTBUG 51712 workaround in
		// file_dialog.cpp, and warranted by Q_ASSERT in registerFormat().
		if (filter.startsWith(format->filter().leftRef(format->filter().length()-1)))
			return format;
	}
	return nullptr;
}

const FileFormat *FileFormatRegistry::findFormatForFilename(const QString& filename) const
{
	QString file_extension = QFileInfo(filename).suffix();
	for (auto format : fmts)
	{
		if (format->fileExtensions().contains(file_extension, Qt::CaseInsensitive)) return format;
	}
	return nullptr;
}
