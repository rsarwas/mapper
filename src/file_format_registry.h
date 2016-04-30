/*
 *    Copyright 2012, 2013 Pete Curtis
 *    Copyright 2013, 2016 Kai Pastor
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

#ifndef _OPENORIENTEERING_FILE_FORMAT_REGISTRY_H
#define _OPENORIENTEERING_FILE_FORMAT_REGISTRY_H

#include <memory>
#include <vector>

#include <QHash>
#include <QVariant>

class QIODevice;

class Importer;
class Exporter;
class Map;
class MapView;

class FileFormat;

/** Provides a registry for file formats, and takes ownership of the supported format objects.
 */
class FileFormatRegistry
{
public:
	/** Creates an empty file format registry.
	 */
	FileFormatRegistry() {};
	
	/** Destroys a file format registry.
	 */
	~FileFormatRegistry();
	
	/** Returns an immutable list of the available file formats.
	 */
	inline const std::vector<FileFormat *> &formats() const { return fmts; }
	
	/** Finds a file format with the given internal ID, or returns NULL if no format
	 *  is found.
	 */
	const FileFormat *findFormat(const char* id) const;
	
	/** Finds a file format which implements the given filter, or returns NULL if no 
	 * format is found.
	 */
	const FileFormat *findFormatByFilter(const QString& filter) const;
	
	/** Finds a file format whose file extension matches the fie extension of the given
	 *  path, or returns NULL if no matching format is found.
	 */
	const FileFormat *findFormatForFilename(const QString& filename) const;
	
	/** Returns the ID of default file format for this registry. This will automatically
	 *  be set to the first registered format.
	 */
	const char* defaultFormat() const { return default_format_id; }
	
	/** Registers a new file format. The registry takes ownership of the provided Format.
	 */
	void registerFormat(FileFormat *format);
	
	/**
	 * Unregisters a file format.
	 * 
	 * Returns a non-const pointer to the file format and transfers ownership to the caller.
	 */
	std::unique_ptr<FileFormat> unregisterFormat(const FileFormat *format);
	
private:
	std::vector<FileFormat *> fmts;
	const char* default_format_id;
};

/** A FileFormatRegistry that is globally defined for convenience. Within the scope of a single
 *  application, you can simply use calls of the form "FileFormats.findFormat(...)".
 */
extern FileFormatRegistry FileFormats;


#endif // _OPENORIENTEERING_FILE_FORMAT_REGISTRY_H
