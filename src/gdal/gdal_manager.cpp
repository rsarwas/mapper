/*
 *    Copyright 2016 Kai Pastor
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

#include "gdal_manager.h"

#include <ogr_api.h>
#include <cpl_conv.h>

#include <QByteArray>
#include <QSettings>

#include "../mapper_resource.h"



namespace
{
	const QString gdal_manager_group{ QString::fromLatin1("GdalManager") };
	const QString gdal_configuration_group{ QString::fromLatin1("GdalConfiguration") };
	const QString gdal_dxf_key{ QString::fromLatin1("dxf") };
	const QString gdal_gpx_key{ QString::fromLatin1("gpx") };
	const QString gdal_osm_key{ QString::fromLatin1("osm") };
	
}



class GdalManager::GdalManagerPrivate
{
public:
	GdalManagerPrivate()
	: dirty{ true }
	{
		// GDAL 2.0: GDALAllRegister();
		OGRRegisterAll();
	}
	
	void configure()
	{
		if (dirty)
			update();
	}
	
	void setFormatEnabled(GdalManager::FileFormat format, bool enabled)
	{
		QString key;
		switch (format)
		{
		case GdalManager::DXF:
			key = gdal_dxf_key;
			break;
			
		case GdalManager::GPX:
			key = gdal_gpx_key;
			break;
			
		case GdalManager::OSM:
			key = gdal_osm_key;
			break;
		}
		QSettings settings;
		settings.beginGroup(gdal_manager_group);
		settings.setValue(key, QVariant{ enabled });
		dirty = true;
	}
	
	bool isFormatEnabled(FileFormat format) const
	{
		QString key;
		switch (format)
		{
		case GdalManager::DXF:
			key = gdal_dxf_key;
			break;
			
		case GdalManager::GPX:
			key = gdal_gpx_key;
			break;
			
		case GdalManager::OSM:
			key = gdal_osm_key;
			break;
		}
		QSettings settings;
		settings.beginGroup(gdal_manager_group);
		return settings.value(key).toBool();
	}
	
	const std::vector<QByteArray>& supportedRasterExtensions() const
	{
		/// \todo
		static std::vector<QByteArray> ret;
		return ret; 
	}
	
	const std::vector<QByteArray>& supportedVectorExtensions() const
	{
		if (dirty)
			update();
		return enabled_vector_extensions;
	}
	
	QStringList parameterKeys() const
	{
		if (dirty)
			update();
		return applied_parameters;
	}
	
	QString parameterValue(const QString& key) const
	{
		if (dirty)
			update();
		QSettings settings;
		settings.beginGroup(gdal_configuration_group);
		return settings.value(key).toString();
	}

	void setParameterValue(const QString& key, const QString& value)
	{
		QSettings settings;
		settings.beginGroup(gdal_configuration_group);
		settings.setValue(key, QVariant{ value });
		dirty = true;
	}
	
	void unsetParameter(const QString& key)
	{
		QSettings settings;
		settings.beginGroup(gdal_configuration_group);
		settings.remove(key);
		dirty = true;
	}
	
private:
	void update() const
	{
		QSettings settings;
		
		/// \todo Build from driver list in GDAL/OGR >= 2.0
		static const std::vector<QByteArray> default_extensions = { "shp", "shx" };
		enabled_vector_extensions.reserve(default_extensions.size() + 3);
		enabled_vector_extensions = default_extensions;
		
		settings.beginGroup(gdal_manager_group);
		if (settings.value(gdal_dxf_key).toBool())
			enabled_vector_extensions.push_back("dxf");
		if (settings.value(gdal_gpx_key).toBool())
			enabled_vector_extensions.push_back("gpx");
		if (settings.value(gdal_osm_key).toBool())
			enabled_vector_extensions.push_back("osm");
		settings.endGroup();
		
		auto gdal_data = MapperResource::locate(MapperResource::GDAL_DATA);
		if (!gdal_data.isEmpty())
		{
			// The user may overwrite this default in the settings.
			CPLSetConfigOption("GDAL_DATA", gdal_data.toLatin1());
		}
		
		settings.beginGroup(gdal_configuration_group);
		QStringList new_parameters = settings.childKeys();
		if (new_parameters.isEmpty())
		{
			// Default options for debugging and for some drivers
			settings.setValue(QString::fromLatin1("CPL_DEBUG"), QVariant{QLatin1String("OFF")});
			settings.setValue(QString::fromLatin1("USE_PROJ_480_FEATURES"), QVariant{QLatin1String("YES")});
			settings.setValue(QString::fromLatin1("OSM_USE_CUSTOM_INDEXING"), QVariant{QLatin1String("NO")});
			new_parameters = settings.childKeys();
		}
		
		new_parameters.sort();
		for (auto parameter : new_parameters)
		{
			CPLSetConfigOption(parameter.toLatin1(), settings.value(parameter).toByteArray());
		}
		for (auto parameter : static_cast<const QStringList&>(applied_parameters))
		{
			if (!new_parameters.contains(parameter)
			    && parameter != QLatin1String{ "GDAL_DATA" })
			{
				CPLSetConfigOption(parameter.toLatin1(), nullptr);
			}
		}
		applied_parameters.swap(new_parameters);
		
		dirty = false;
	}
	
	mutable bool dirty;
	
	mutable std::vector<QByteArray> enabled_vector_extensions;
	
	mutable QStringList applied_parameters;
	
};



// ### GdalManager ###

GdalManager::GdalManager()
{
	static GdalManagerPrivate manager;
	p = &manager;
}

void GdalManager::configure()
{
	p->configure();
}

void GdalManager::setFormatEnabled(GdalManager::FileFormat format, bool enabled)
{
	p->setFormatEnabled(format, enabled);
}

bool GdalManager::isFormatEnabled(GdalManager::FileFormat format) const
{
	return p->isFormatEnabled(format);
}

const std::vector<QByteArray>&GdalManager::supportedRasterExtensions() const
{
	return p->supportedRasterExtensions();
}

const std::vector<QByteArray>& GdalManager::supportedVectorExtensions() const
{
	return p->supportedVectorExtensions();
}

QStringList GdalManager::parameterKeys() const
{
	return p->parameterKeys();
}

QString GdalManager::parameterValue(const QString& key) const
{
	return p->parameterValue(key);
}

void GdalManager::setParameterValue(const QString& key, const QString& value)
{
	p->setParameterValue(key, value);
}

void GdalManager::unsetParameter(const QString& key)
{
	p->unsetParameter(key);
}
