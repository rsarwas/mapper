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

#include "ogr_file_format.h"
#include "ogr_file_format_p.h"

#include <memory>

#include <cpl_error.h>
#include <cpl_conv.h>
#include <ogr_srs_api.h>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtMath>

#include "gdal_manager.h"
#include "../core/georeferencing.h"
#include "../map.h"
#include "../object_text.h"
#include "../symbol_area.h"
#include "../symbol_line.h"
#include "../symbol_point.h"
#include "../symbol_text.h"


namespace ogr
{
	class OGRDataSourceHDeleter
	{
	public:
		void operator()(OGRDataSourceH data_source) const
		{
			OGRReleaseDataSource(data_source);
		}
	};
	
	/** A convenience class for OGR C API datasource handles, similar to std::unique_ptr. */
	using unique_datasource = std::unique_ptr<typename std::remove_pointer<OGRDataSourceH>::type, OGRDataSourceHDeleter>;
	
	
	class OGRFeatureHDeleter
	{
	public:
		void operator()(OGRFeatureH feature) const
		{
			OGR_F_Destroy(feature);
		}
	};
	
	/** A convenience class for OGR C API feature handles, similar to std::unique_ptr. */
	using unique_feature = std::unique_ptr<typename std::remove_pointer<OGRFeatureH>::type, OGRFeatureHDeleter>;
	
}

namespace
{
	void applyPenWidth(OGRStyleToolH tool, LineSymbol* line_symbol)
	{
		int is_null;
		auto pen_width = OGR_ST_GetParamDbl(tool, OGRSTPenWidth, &is_null);
		if (!is_null)
		{
			Q_ASSERT(OGR_ST_GetUnit(tool) == OGRSTUMM);
	
			if (pen_width <= 0.01)
				pen_width = 0.1;
			
			line_symbol->setLineWidth(pen_width);
		}
	}
	
	void applyPenCap(OGRStyleToolH tool, LineSymbol* line_symbol)
	{
		int is_null;
		auto pen_cap = OGR_ST_GetParamStr(tool, OGRSTPenCap, &is_null);
		if (!is_null)
		{
			switch (pen_cap[0])
			{
			case 'p':
				line_symbol->setCapStyle(LineSymbol::SquareCap);
				break;
			case 'r':
				line_symbol->setCapStyle(LineSymbol::RoundCap);
				break;
			default:
				;
			}
		}
	}

	void applyPenJoin(OGRStyleToolH tool, LineSymbol* line_symbol)
	{
		int is_null;
		auto pen_join = OGR_ST_GetParamStr(tool, OGRSTPenJoin, &is_null);
		if (!is_null)
		{
			switch (pen_join[0])
			{
			case 'b':
				line_symbol->setJoinStyle(LineSymbol::BevelJoin);
				break;
			case 'r':
				line_symbol->setJoinStyle(LineSymbol::RoundJoin);
				break;
			default:
				;
			}
		}
	}
	
	void applyPenPattern(OGRStyleToolH tool, LineSymbol* line_symbol)
	{
		int is_null;
		auto raw_pattern = OGR_ST_GetParamStr(tool, OGRSTPenPattern, &is_null);
		if (!is_null)
		{
			auto pattern = QString::fromLatin1(raw_pattern);
			auto sub_pattern_re = QRegularExpression(QString::fromLatin1("([0-9.]+)([a-z]*) *([0-9.]+)([a-z]*)"));
			auto match = sub_pattern_re.match(pattern);
			double length_0, length_1;
			bool ok = match.hasMatch();
			if (ok)
				length_0 = match.capturedRef(1).toDouble(&ok);
			if (ok)
				length_1 = match.capturedRef(3).toDouble(&ok);
			if (ok)
			{
				/// \todo Apply units from capture 2 and 4
				line_symbol->setDashed(true);
				line_symbol->setDashLength(qMax(100, qRound(length_0 * 1000)));
				line_symbol->setBreakLength(qMax(100, qRound(length_1 * 1000)));
			}
			else
			{
				qDebug("OgrFileFormat: Failed to parse dash pattern '%s'", raw_pattern);
			}
		}
	}
	
	int getFontSize(const char* font_size_string)
	{
		auto pattern = QString::fromLatin1(font_size_string);
		auto sub_pattern_re = QRegularExpression(QString::fromLatin1("([0-9.]+)([a-z]*)"));
		auto match = sub_pattern_re.match(pattern);
		double font_size;
		bool ok = match.hasMatch();
		if (ok)
			font_size = match.capturedRef(1).toDouble(&ok);
		if (ok)
		{
			auto unit = match.capturedRef(2).toUtf8();
			if (!unit.isEmpty())
			{
				if (unit == "pt")
				{
					
				}
				else if (unit == "px")
				{
					
				}
				else
				{
					qDebug("OgrFileFormat: Unsupported font size unit '%s'", unit.constData());
				}
			}
		}
		else
		{
			qDebug("OgrFileFormat: Failed to parse font size '%s'", font_size_string);
			font_size = 0;
		}
		return font_size;
	}
	
	void applyLabelAnchor(int anchor, TextObject* text_object)
	{
		auto v_align = (anchor - 1) / 3;
		switch (v_align)
		{
		case 0:
			text_object->setVerticalAlignment(TextObject::AlignBaseline);
			break;
		case 1:
			text_object->setVerticalAlignment(TextObject::AlignVCenter);
			break;
		case 2:
			text_object->setVerticalAlignment(TextObject::AlignTop);
			break;
		case 3:
			text_object->setVerticalAlignment(TextObject::AlignBottom);
			break;
		default:
			Q_UNREACHABLE();
		}
		auto h_align = (anchor - 1) % 3;
		switch (h_align)
		{
		case 0:
			text_object->setHorizontalAlignment(TextObject::AlignLeft);
			break;
		case 1:
			text_object->setHorizontalAlignment(TextObject::AlignHCenter);
			break;
		case 2:
			text_object->setHorizontalAlignment(TextObject::AlignRight);
			break;
		default:
			Q_UNREACHABLE();
		}
	}
}



// ### OgrFileFormat ###

OgrFileFormat::OgrFileFormat()
 : FileFormat(OgrFile, "OGR", ImportExport::tr("Geospatial vector data"), QString{}, ImportSupported)
{
	for (const auto extension : GdalManager().supportedVectorExtensions())
		addExtension(QString::fromLatin1(extension));
}

bool OgrFileFormat::understands(const unsigned char*, size_t) const
{
	return true;
}

Importer* OgrFileFormat::createImporter(QIODevice* stream, Map *map, MapView *view) const
{
	return new OgrFileImport(stream, map, view);
}



// ### OgrFileImport ###

OgrFileImport::OgrFileImport(QIODevice* stream, Map* map, MapView* view, bool drawing_from_projected)
 : Importer(stream, map, view)
 , map_srs{ OSRNewSpatialReference(nullptr) }
 , manager{ OGR_SM_Create(nullptr) }
 , drawing_from_projected{ drawing_from_projected }
{
	GdalManager().configure();
	
	setOption(QLatin1String{ "Separate layers" }, QVariant{ false });
	
	auto spec = QByteArray::fromRawData("WGS84", 6);
	auto error = OSRSetWellKnownGeogCS(map_srs.get(), spec);
	if (!map->getGeoreferencing().isLocal() && !error)
	{
		spec = map->getGeoreferencing().getProjectedCRSSpec().toLatin1();
		error = OSRImportFromProj4(map_srs.get(), spec);
	}
	
	if (error)
	{
		addWarning(tr("Unable to setup \"%1\" SRS for GDAL: %2")
		           .arg(QString::fromLatin1(spec), QString::number(error)));
	}
	
	// Reasonable default?
	
	// OGR feature style defaults
	default_pen_color = new MapColor(tr("Black"), 0); 
	default_pen_color->setRgb({0.0, 0.0, 0.0});
	default_pen_color->setCmykFromRgb();
	map->addColor(default_pen_color, 0);
	
	auto default_brush_color = new MapColor(tr("Black") + QLatin1String(" 50%"), 0);
	default_brush_color->setRgb({0.5, 0.5, 0.5});
	default_brush_color->setCmykFromRgb();
	map->addColor(default_brush_color, 1);
	
	default_point_symbol = new PointSymbol();
	default_point_symbol->setName(tr("Point"));
	default_point_symbol->setNumberComponent(0, 1);
	default_point_symbol->setInnerColor(default_pen_color);
	map->addSymbol(default_point_symbol, 0);
	
	default_line_symbol = new LineSymbol();
	default_line_symbol->setName(tr("Line"));
	default_line_symbol->setNumberComponent(0, 2);
	default_line_symbol->setColor(default_pen_color);
	default_line_symbol->setLineWidth(0.1); // (0.1 mm, nearly cosmetic)
	default_line_symbol->setCapStyle(LineSymbol::FlatCap);
	default_line_symbol->setJoinStyle(LineSymbol::MiterJoin);
	map->addSymbol(default_line_symbol, 1);
	
	default_area_symbol = new AreaSymbol();
	default_area_symbol->setName(tr("Area"));
	default_area_symbol->setNumberComponent(0, 3);
	default_area_symbol->setColor(default_brush_color);
	map->addSymbol(default_area_symbol, 2);
	
	default_text_symbol = new TextSymbol();
	default_text_symbol->setName(tr("Text"));
	default_text_symbol->setNumberComponent(0, 4);
	default_text_symbol->setColor(default_pen_color);
	map->addSymbol(default_text_symbol, 3);
}

OgrFileImport::~OgrFileImport()
{
	// nothing
}

void OgrFileImport::import(bool load_symbols_only)
{
	auto file = qobject_cast<QFile*>(stream);
	if (!file)
	{
		throw FileFormatException("Internal error"); /// \todo Review design and/or message
	}
	
	auto filename = file->fileName();
	// GDAL 2.0: ... = GDALOpenEx(template_path.toLatin1(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
	auto data_source = ogr::unique_datasource(OGROpen(filename.toLatin1(), 0, nullptr));
	if (data_source == nullptr)
	{
		throw FileFormatException(Importer::tr("Could not read '%1'")
		                          .arg(filename));
	}
	
	empty_geometries = 0;
	no_transformation = 0;
	failed_transformation = 0;
	unsupported_geometry_type = 0;
	too_few_coordinates = 0;
	
	importStyles(data_source.get());

	if (!load_symbols_only)
	{
		auto num_layers = OGR_DS_GetLayerCount(data_source.get());
		for (int i = 0; i < num_layers; ++i)
		{
			auto layer = OGR_DS_GetLayer(data_source.get(), i);
			if (!layer)
			{
				addWarning(tr("Unable to load layer %1.").arg(i));
				continue;
			}
			
			auto part = map->getCurrentPart();
			if (option(QLatin1String("Separate layers")).toBool())
			{
				if (num_layers > 0)
				{
					if (part->getNumObjects() == 0)
					{
						part->setName(QString::fromUtf8(OGR_L_GetName(layer)));
					}
					else
					{
						part = new MapPart(QString::fromUtf8(OGR_L_GetName(layer)), map);
						auto index = map->getNumParts();
						map->addPart(part, index);
						map->setCurrentPartIndex(index);
					}
				}
			}
				
			importLayer(part, layer);
		}
	}
	
	if (empty_geometries)
	{
		addWarning(tr("Unable to load %n objects, reason: %1", nullptr, empty_geometries)
		           .arg(tr("Empty geometry.")));
	}
	if (no_transformation)
	{
		addWarning(tr("Unable to load %n objects, reason: %1", nullptr, no_transformation)
		           .arg(tr("Can't determine the coordinate transformation: %1").arg(QString::fromUtf8(CPLGetLastErrorMsg()))));
	}
	if (failed_transformation)
	{
		addWarning(tr("Unable to load %n objects, reason: %1", nullptr, failed_transformation)
		           .arg(tr("Failed to transform the coordinates.")));
	}
	if (unsupported_geometry_type)
	{
		addWarning(tr("Unable to load %n objects, reason: %1", nullptr, unsupported_geometry_type)
		           .arg(tr("Unknown or unsupported geometry type.")));
	}
	if (too_few_coordinates)
	{
		addWarning(tr("Unable to load %n objects, reason: %1", nullptr, too_few_coordinates)
		           .arg(tr("Not enough coordinates.")));
	}
}

void OgrFileImport::importStyles(OGRDataSourceH data_source)
{
	//auto style_table = OGR_DS_GetStyleTable(data_source);
	Q_UNUSED(data_source)
}

void OgrFileImport::importLayer(MapPart* map_part, OGRLayerH layer)
{
	Q_ASSERT(map_part);
	
	auto feature_definition = OGR_L_GetLayerDefn(layer);
	
	OGR_L_ResetReading(layer);
	while (auto feature = ogr::unique_feature(OGR_L_GetNextFeature(layer)))
	{
		auto geometry = OGR_F_GetGeometryRef(feature.get());
		if (!geometry || OGR_G_IsEmpty(geometry))
		{
			++empty_geometries;
			continue;
		}
		
		OGR_G_FlattenTo2D(geometry);
		importFeature(map_part, feature_definition, feature.get(), geometry);
	}
}

void OgrFileImport::importFeature(MapPart* map_part, OGRFeatureDefnH feature_definition, OGRFeatureH feature, OGRGeometryH geometry)
{
	to_map_coord = &OgrFileImport::fromProjected;
	auto new_srs = OGR_G_GetSpatialReference(geometry);
	if (new_srs && data_srs != new_srs)
	{
		// New SRS, indeed.
		
		auto transformation = ogr::unique_transformation{ OCTNewCoordinateTransformation(new_srs, map_srs.get()) };
		if (!transformation)
		{
			++no_transformation;
			return;
		}
		
		// Commit change to data srs and coordinate transformation
		data_srs = new_srs;
		data_transform = std::move(transformation);
	}
	
	if (new_srs)
	{
		auto error = OGR_G_Transform(geometry, data_transform.get());
		if (error)
		{
			++failed_transformation;
			return;
		}
	}
	else if (!drawing_from_projected)
	{
		to_map_coord = &OgrFileImport::fromDrawing;
	}
	
	auto object = importGeometry(map_part, feature, geometry);
	
	if (object && feature_definition)
	{
		auto num_fields = OGR_FD_GetFieldCount(feature_definition);
		for (int i = 0; i < num_fields; ++i)
		{
			auto value = OGR_F_GetFieldAsString(feature, i);
			if (value && qstrlen(value) > 0)
			{
				auto field_definition = OGR_FD_GetFieldDefn(feature_definition, i);
				object->setTag(QString::fromUtf8(OGR_Fld_GetNameRef(field_definition)), QString::fromUtf8(value));
			}
		}
	}
}

Object* OgrFileImport::importGeometry(MapPart* map_part, OGRFeatureH feature, OGRGeometryH geometry)
{
	auto geometry_type = wkbFlatten(OGR_G_GetGeometryType(geometry));
	switch (geometry_type)
	{
	case OGRwkbGeometryType::wkbPoint:
		return importPointGeometry(map_part, feature, geometry);
		
	case OGRwkbGeometryType::wkbLineString:
		return importLineStringGeometry(map_part, feature, geometry);
		
	case OGRwkbGeometryType::wkbPolygon:
		return importPolygonGeometry(map_part, feature, geometry);
		
	case OGRwkbGeometryType::wkbGeometryCollection:
	case OGRwkbGeometryType::wkbMultiLineString:
	case OGRwkbGeometryType::wkbMultiPoint:
	case OGRwkbGeometryType::wkbMultiPolygon:
		return importGeometryCollection(map_part, feature, geometry);
		
	default:
		qDebug("OgrFileImport: Unknown or unsupported geometry type: %d", geometry_type);
		++unsupported_geometry_type;
		return nullptr;
	}
}

Object* OgrFileImport::importGeometryCollection(MapPart* map_part, OGRFeatureH feature, OGRGeometryH geometry)
{
	auto num_geometries = OGR_G_GetGeometryCount(geometry);
	for (int i = 0; i < num_geometries; ++i)
	{
		importGeometry(map_part, feature, OGR_G_GetGeometryRef(geometry, i));
	}
	return nullptr;
}

Object* OgrFileImport::importPointGeometry(MapPart* map_part, OGRFeatureH feature, OGRGeometryH geometry)
{
	auto style = OGR_F_GetStyleString(feature);
	auto symbol = getSymbol(Symbol::Point, style);
	if (symbol->getType() == Symbol::Point)
	{
		auto object = new PointObject(symbol);
		object->setPosition(toMapCoord(OGR_G_GetX(geometry, 0), OGR_G_GetY(geometry, 0)));
		map_part->addObject(object);
		return object;
	}
	else if (symbol->getType() == Symbol::Text)
	{
		const auto& description = symbol->getDescription();
		auto length = description.length();
		auto split = description.indexOf(QLatin1Char(' '));
		Q_ASSERT(split > 0);
		Q_ASSERT(split < length);
		
		auto label = description.right(length - split - 1);
		if (label.startsWith(QLatin1Char{'{'}) && label.endsWith(QLatin1Char{'}'}))
		{
			label.remove(0, 1);
			label.chop(1);
			int index = OGR_F_GetFieldIndex(feature, label.toLatin1());
			if (index >= 0)
			{
				label = QString::fromUtf8(OGR_F_GetFieldAsString(feature, index));
			}
		}
		if (!label.isEmpty())
		{
			auto object = new TextObject(symbol);
			object->setAnchorPosition(toMapCoord(OGR_G_GetX(geometry, 0), OGR_G_GetY(geometry, 0)));
			// DXF observation
			label.replace(QRegularExpression(QString::fromLatin1("(\\\\[^;]*;)*"), QRegularExpression::MultilineOption), QString{});
			label.replace(QLatin1String("^I"), QLatin1String("\t"));
			object->setText(label);
			
			bool ok;
			auto anchor = QStringRef(&description, 1, 2).toInt(&ok);
			if (ok)
			{
				applyLabelAnchor(anchor, object);
			}
				
			auto angle = QStringRef(&description, 3, split-3).toFloat(&ok);
			if (ok)
			{
				object->setRotation(qDegreesToRadians(angle));
			}
			
			map_part->addObject(object);
			return object;
		}
	}
	
	return nullptr;
}

PathObject* OgrFileImport::importLineStringGeometry(MapPart* map_part, OGRFeatureH feature, OGRGeometryH geometry)
{
	geometry = OGR_G_ForceToLineString(geometry);
	
	auto num_points = OGR_G_GetPointCount(geometry);
	if (num_points < 2)
	{
		++too_few_coordinates;
		return nullptr;
	}
	
	auto style = OGR_F_GetStyleString(feature);
	auto object = new PathObject(getSymbol(Symbol::Line, style));
	for (int i = 0; i < num_points; ++i)
	{
		object->addCoordinate(toMapCoord(OGR_G_GetX(geometry, i), OGR_G_GetY(geometry, i)));
	}
	map_part->addObject(object);
	return object;
}

PathObject* OgrFileImport::importPolygonGeometry(MapPart* map_part, OGRFeatureH feature, OGRGeometryH geometry)
{
	auto num_geometries = OGR_G_GetGeometryCount(geometry);
	if (num_geometries < 1)
	{
		++too_few_coordinates;
		return nullptr;
	}
	
	auto outline = OGR_G_ForceToLineString(OGR_G_GetGeometryRef(geometry, 0));
	auto num_points = OGR_G_GetPointCount(outline);
	if (num_points < 3)
	{
		++too_few_coordinates;
		return nullptr;
	}
	
	auto style = OGR_F_GetStyleString(feature);
	auto object = new PathObject(getSymbol(Symbol::Area, style));
	for (int i = 0; i < num_points; ++i)
	{
		object->addCoordinate(toMapCoord(OGR_G_GetX(outline, i), OGR_G_GetY(outline, i)));
	}
	
	for (int g = 1; g < num_geometries; ++g)
	{
		bool start_new_part = true;
		auto hole = /*OGR_G_ForceToLineString*/(OGR_G_GetGeometryRef(geometry, g));
		auto num_points = OGR_G_GetPointCount(hole);
		for (int i = 0; i < num_points; ++i)
		{
			object->addCoordinate(toMapCoord(OGR_G_GetX(hole, i), OGR_G_GetY(hole, i)), start_new_part);
			start_new_part = false;
		}
	}
	
	object->closeAllParts();
	map_part->addObject(object);
	return object;
}

Symbol* OgrFileImport::getSymbol(Symbol::Type type, const char* raw_style_string)
{
	auto style_string = QByteArray::fromRawData(raw_style_string, qstrlen(raw_style_string));
	Symbol* symbol = nullptr;
	switch (type)
	{
	case Symbol::Point:
	case Symbol::Text:
		symbol = point_symbols.value(style_string);
		if (!symbol)
			symbol = getSymbolForPointGeometry(style_string);
		break;
		
	case Symbol::Combined:
		/// \todo
		//  fall through
	case Symbol::Line:
		symbol = line_symbols.value(style_string);
		if (!symbol)
			symbol = getLineSymbol(style_string);
		if (!symbol)
			symbol = default_line_symbol;
		break;
		
	case Symbol::Area:
		symbol = area_symbols.value(style_string);
		if (!symbol)
			symbol = getAreaSymbol(style_string);
		if (!symbol)
			symbol = default_area_symbol;
		break;
		
	case Symbol::NoSymbol:
	case Symbol::AllSymbols:
		Q_UNREACHABLE();
	}
	
	Q_ASSERT(symbol);
	return symbol;
}

MapColor* OgrFileImport::makeColor(OGRStyleToolH tool, const char* color_string)
{
	auto key = QByteArray::fromRawData(color_string, qstrlen(color_string));
	auto color = colors.value(key);
	if (!color)
	{	
		int r, g, b, a;
		auto success = OGR_ST_GetRGBFromString(tool, color_string, &r, &g, &b, &a);
		if (!success)
		{
			color = default_pen_color;
		}
		else if (a > 0)
		{
			color = new MapColor(QString::fromUtf8(color_string), map->getNumColors());
			color->setRgb(QColor{ r, g, b });
			color->setCmykFromRgb();
			map->addColor(color, map->getNumColors());
		}
		
		key.detach();
		colors.insert(key, color);
	}
	
	return color;
}

void OgrFileImport::applyPenColor(OGRStyleToolH tool, LineSymbol* line_symbol)
{
	int is_null;
	auto color_string = OGR_ST_GetParamStr(tool, OGRSTPenColor, &is_null);
	if (!is_null)
	{
		auto color = makeColor(tool, color_string);
		if (color)
			line_symbol->setColor(color);
		else
			line_symbol->setHidden(true);
	}
}

void OgrFileImport::applyBrushColor(OGRStyleToolH tool, AreaSymbol* area_symbol)
{
	int is_null;
	auto color_string = OGR_ST_GetParamStr(tool, OGRSTBrushFColor, &is_null);
	if (!is_null)
	{
		auto color = makeColor(tool, color_string);
		if (color)
			area_symbol->setColor(color);
		else
			area_symbol->setHidden(true);
	}
}


Symbol* OgrFileImport::getSymbolForPointGeometry(const QByteArray& style_string)
{
	if (style_string.isEmpty())
		return default_point_symbol;
	
	auto manager = this->manager.get();
	
	auto data = style_string.constData();
	if (!OGR_SM_InitStyleString(manager, data))
		return default_point_symbol;
	
	auto num_parts = OGR_SM_GetPartCount(manager, data);
	if (!num_parts)
		return default_point_symbol;
	
	Symbol* symbol = nullptr;
	for (int i = 0; !symbol && i < num_parts; ++i)
	{
		auto tool = OGR_SM_GetPart(manager, i, nullptr);
		if (!tool)
			continue;
		
		OGR_ST_SetUnit(tool, OGRSTUMM, map->getScaleDenominator());
		
		auto type = OGR_ST_GetType(tool);
		switch (type)
		{
		case OGRSTCSymbol:
			symbol = getSymbolForOgrSymbol(tool, style_string);
			break;
			
		case OGRSTCLabel:
			symbol = getSymbolForLabel(tool, style_string);
			break;
			
		default:
			;
		}
		
		OGR_ST_Destroy(tool);
	}
	
	return symbol;
}

LineSymbol* OgrFileImport::getLineSymbol(const QByteArray& style_string)
{
	if (style_string.isEmpty())
		return nullptr;
	
	auto manager = this->manager.get();
	
	auto data = style_string.constData();
	if (!OGR_SM_InitStyleString(manager, data))
		return nullptr;
	
	auto num_parts = OGR_SM_GetPartCount(manager, data);
	if (!num_parts)
		return nullptr;
	
	LineSymbol* symbol = nullptr;
	for (int i = 0; !symbol && i < num_parts; ++i)
	{
		auto tool = OGR_SM_GetPart(manager, i, nullptr);
		if (!tool)
			continue;
		
		OGR_ST_SetUnit(tool, OGRSTUMM, map->getScaleDenominator());
		
		auto type = OGR_ST_GetType(tool);
		switch (type)
		{
		case OGRSTCPen:
			symbol = getSymbolForPen(tool, style_string);
			break;
			
		default:
			;
		}
		
		OGR_ST_Destroy(tool);
	}
	
	return symbol;
}

AreaSymbol* OgrFileImport::getAreaSymbol(const QByteArray& style_string)
{
	if (style_string.isEmpty())
		return nullptr;
	
	auto manager = this->manager.get();
	
	auto data = style_string.constData();
	if (!OGR_SM_InitStyleString(manager, data))
		return nullptr;
	
	auto num_parts = OGR_SM_GetPartCount(manager, data);
	if (!num_parts)
		return nullptr;
	
	AreaSymbol* symbol = nullptr;
	for (int i = 0; !symbol && i < num_parts; ++i)
	{
		auto tool = OGR_SM_GetPart(manager, i, nullptr);
		if (!tool)
			continue;
		
		OGR_ST_SetUnit(tool, OGRSTUMM, map->getScaleDenominator());
		
		auto type = OGR_ST_GetType(tool);
		switch (type)
		{
		case OGRSTCBrush:
			symbol = getSymbolForBrush(tool, style_string);
			break;
			
		default:
			;
		}
		
		OGR_ST_Destroy(tool);
	}
	
	return symbol;
}

PointSymbol* OgrFileImport::getSymbolForOgrSymbol(OGRStyleToolH tool, const QByteArray& style_string)
{
	Q_ASSERT(OGR_ST_GetType(tool) == OGRSTCSymbol);
	
	auto raw_tool_key = OGR_ST_GetStyleString(tool);
	auto tool_key = QByteArray::fromRawData(raw_tool_key, qstrlen(raw_tool_key));
	auto symbol = point_symbols.value(tool_key);
	if (symbol && symbol->getType() == Symbol::Point)
		return static_cast<PointSymbol*>(symbol);
	
	int is_null;
	auto color_string = OGR_ST_GetParamStr(tool, OGRSTSymbolColor, &is_null);
	if (is_null)
		return nullptr;
	
	auto point_symbol = static_cast<PointSymbol*>(default_point_symbol->duplicate());
	auto color = makeColor(tool, color_string);
	if (color)
		point_symbol->setInnerColor(color);
	else
		point_symbol->setHidden(true);
	
	auto key = style_string;
	key.detach();
	point_symbols.insert(key, point_symbol);
	
	if (key != tool_key)
	{
		tool_key.detach();
		point_symbols.insert(tool_key, point_symbol);
	}
	
	map->addSymbol(point_symbol, map->getNumSymbols());
	return point_symbol;
}

TextSymbol* OgrFileImport::getSymbolForLabel(OGRStyleToolH tool, const QByteArray&)
{
	Q_ASSERT(OGR_ST_GetType(tool) == OGRSTCLabel);
	
	int is_null;
	auto label_string = OGR_ST_GetParamStr(tool, OGRSTLabelTextString, &is_null);
	if (is_null)
		return nullptr;
	
	auto color_string = OGR_ST_GetParamStr(tool, OGRSTLabelFColor, &is_null);
	auto font_size_string = OGR_ST_GetParamStr(tool, OGRSTLabelSize, &is_null);
	
	// Don't use the style string as a key: The style contains the label.
	QByteArray key;
	key.reserve(qstrlen(color_string) + qstrlen(font_size_string) + 1);
	key.append(color_string);
	key.append(font_size_string);
	auto text_symbol = static_cast<TextSymbol*>(text_symbols.value(key));
	if (!text_symbol)
	{
		text_symbol = static_cast<TextSymbol*>(default_text_symbol->duplicate());
		
		auto color = makeColor(tool, color_string);
		if (color)
			text_symbol->setColor(color);
		else
			text_symbol->setHidden(true);
		
		auto font_size = OGR_ST_GetParamDbl(tool, OGRSTLabelSize, &is_null);
		if (!is_null && font_size > 0.0)
			text_symbol->scale(font_size / text_symbol->getFontSize());
		
		key.detach();
		text_symbols.insert(key, text_symbol);
		
		map->addSymbol(text_symbol, map->getNumSymbols());
	}
	
	auto anchor = qBound(1, OGR_ST_GetParamNum(tool, OGRSTLabelAnchor, &is_null), 12);
	if (is_null)
		anchor = 1;
	
	auto angle = OGR_ST_GetParamDbl(tool, OGRSTLabelAngle, &is_null);
	if (is_null)
		angle = 0.0;
	
	QString description;
	description.reserve(qstrlen(label_string) + 100);
	description.append(QString::number(100 + anchor));
	description.append(QString::number(angle, 'g', 1));
	description.append(QLatin1Char(' '));
	description.append(QString::fromUtf8(label_string));
	text_symbol->setDescription(description);
	
	return text_symbol;
}

LineSymbol* OgrFileImport::getSymbolForPen(OGRStyleToolH tool, const QByteArray& style_string)
{
	Q_ASSERT(OGR_ST_GetType(tool) == OGRSTCPen);
	
	auto raw_tool_key = OGR_ST_GetStyleString(tool);
	auto tool_key = QByteArray::fromRawData(raw_tool_key, qstrlen(raw_tool_key));
	auto symbol = line_symbols.value(tool_key);
	if (symbol && symbol->getType() == Symbol::Line)
		return static_cast<LineSymbol*>(symbol);
	
	auto line_symbol = static_cast<LineSymbol*>(default_line_symbol->duplicate());
	applyPenColor(tool, line_symbol);
	applyPenWidth(tool, line_symbol);
	applyPenCap(tool, line_symbol);
	applyPenJoin(tool, line_symbol);
	applyPenPattern(tool, line_symbol);
	
	auto key = style_string;
	key.detach();
	line_symbols.insert(key, line_symbol);
	
	if (key != tool_key)
	{
		tool_key.detach();
		line_symbols.insert(tool_key, line_symbol);
	}
	
	map->addSymbol(line_symbol, map->getNumSymbols());
	return line_symbol;
}

AreaSymbol* OgrFileImport::getSymbolForBrush(OGRStyleToolH tool, const QByteArray& style_string)
{
	Q_ASSERT(OGR_ST_GetType(tool) == OGRSTCBrush);
	
	auto raw_tool_key = OGR_ST_GetStyleString(tool);
	auto tool_key = QByteArray::fromRawData(raw_tool_key, qstrlen(raw_tool_key));
	auto symbol = area_symbols.value(tool_key);
	if (symbol && symbol->getType() == Symbol::Area)
		return static_cast<AreaSymbol*>(symbol);
	
	auto area_symbol = static_cast<AreaSymbol*>(default_area_symbol->duplicate());
	applyBrushColor(tool, area_symbol);
	
	auto key = style_string;
	key.detach();
	area_symbols.insert(key, area_symbol);
	
	if (key != tool_key)
	{
		tool_key.detach();
		area_symbols.insert(tool_key, area_symbol);
	}
	
	map->addSymbol(area_symbol, map->getNumSymbols());
	return area_symbol;
}


MapCoord OgrFileImport::fromDrawing(double x, double y) const
{
	return { x, -y };
}

MapCoord OgrFileImport::fromProjected(double x, double y) const
{
	return map->getGeoreferencing().toMapCoords(QPointF{ x, y });
}
