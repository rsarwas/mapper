/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2015 Kai Pastor
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


#ifndef _OPENORIENTEERING_TEMPLATE_H_
#define _OPENORIENTEERING_TEMPLATE_H_

#include <memory>

#include "matrix.h"
#include "transformation.h"

QT_BEGIN_NAMESPACE
class QIODevice;
class QLineEdit;
class QPainter;
class QPixmap;
class QRectF;
class QXmlStreamReader;
class QXmlStreamWriter;
QT_END_NAMESPACE

class Map;
class MapView;
class MapWidget;

/** Transformation parameters for non-georeferenced templates */
class TemplateTransform
{
public:
	TemplateTransform();
	
	void load(QIODevice* file);
	
	void save(QXmlStreamWriter& xml, const QString role) const;
 	void load(QXmlStreamReader& xml);
	
	/// Position in 1/1000 mm
	qint32 template_x;
	qint32 template_y;
	/// Scaling relative to "1 painter unit == 1 mm on map"
	double template_scale_x;
	double template_scale_y;
	/// Rotation in radians
	double template_rotation;
};

/** Abstract base class for templates. */
class Template : public QObject
{
Q_OBJECT
public:
	/// States in the lifetime of a template
	enum State
	{
		/// The template is loaded and ready to be displayed
		Loaded = 0,
		/// The template has been unloaded, but can be reloaded if needed
		Unloaded,
		/// A required resource cannot be found (e.g. missing image or font),
		/// so the template is invalid
		Invalid
	};
	
	
	/// To be called by derived classes with main template file and map pointer.
	/// Initializes the template as "invalid".
	Template(const QString& path, Map* map);
	virtual ~Template();
	
	/// Creates a duplicate of the template
	Template* duplicate() const;
	
	/// Returns a string which should identify the type of the template uniquely:
	/// the class name. Very simple RTTI feature.
	virtual const char* getTemplateType() const = 0;
	
	/**
	 * Returns a description of the last error that occurred.
	 */
	QString errorString() const;
	
	/**
	 * Returns true if the template is raster graphics.
	 * 
	 * Raster graphics cannot be printed in the foreground in vector mode.
	 */
	virtual bool isRasterGraphics() const = 0;
	
	
	/// Saves template parameters such as filename, transformation, adjustment, etc. and
	/// type-specific parameters (e.g. filtering mode for images)
	void saveTemplateConfiguration(QXmlStreamWriter& xml, bool open);
	
	/// Loads template parameters, see saveTemplateConfiguration(), and returns true if successful
	bool loadTemplateConfiguration(QIODevice* stream, int version);
	static std::unique_ptr<Template> loadTemplateConfiguration(QXmlStreamReader& xml, Map& map, bool& open);
	
	/// Saves the template itself, returns true if successful.
	/// This is called when saving the map and the template's hasUnsavedChanges() returns true
	virtual bool saveTemplateFile() const {return false;}
	
	/// Changes a template's file without changing the parameters.
	/// Useful when a template file has been moved.
	/// If load_file is true, tries to load the given file.
	void switchTemplateFile(const QString& new_path, bool load_file);
	
	/// Shows the dialog to find a moved template. If the user selects a new file,
	/// tries to switch to the selected template file using switchTemplateFile() and
	/// by trying to load the new file. Returns true if this succeeds; if not, reverts the
	/// switch and returns false. Also returns false if the dialog is aborted.
	bool execSwitchTemplateFileDialog(QWidget* dialog_parent);
	
	
	/// Does preLoadConfiguration(), loadTemplateFile() and postLoadConfiguration() and
	/// returns if the process was successful. Pass in the view in which the template
	/// should be centered, if it is centered.
	bool configureAndLoad(QWidget* dialog_parent, MapView* view);
	
	/// Tries to find and (re-)load the template file from the following positions:
	///  - saved relative position to map file, if available and map_directory is not empty
	///  - absolute position of template file
	///  - template filename in map_directory, if map_directory not empty
	/// Returns true if successful.
	/// If out_loaded_from_map_dir is given, it is set to true if the template file is successfully
	/// loaded using the template filename in the map's directory (3rd option).
	bool tryToFindAndReloadTemplateFile(QString map_directory, bool* out_loaded_from_map_dir = NULL);
	
	/// Does the pre-load configuration when the template is opened initially
	/// (after the user chooses the template file, but before it is loaded).
	/// Derived classes can show dialogs here to get user input which is needed
	/// to interpret the template file.
	/// If the implementation returns false, loading the template is aborted.
	/// NOTE: derived classes should set is_georeferenced either here or
	/// in postLoadConfiguration(). By default templates are loaded as non-georeferenced.
	virtual bool preLoadConfiguration(QWidget* dialog_parent);
	
	/// Loads the template file. Can be called if the template state is Invalid or Unloaded.
	/// Must not be called if the template file is already loaded.
	/// Set the configuring parameter to true if the template is currently being configured
	/// by the user (in contrast to the case where it is loaded from an existing map file).
	/// In this case, the next step is to call postLoadConfiguration().
	bool loadTemplateFile(bool configuring);
	
	/// Does the post-load configuration when the template is opened initially
	/// (after the chosen template file is loaded).
	/// If the implementation returns false, loading the template is aborted.
	/// By setting out_center_in_view, the implementation can decide if the template should
	/// be centered in the active view if it is a non-georeferenced template (on by default).
	virtual bool postLoadConfiguration(QWidget* dialog_parent, bool& out_center_in_view);
	
	/// Unloads the template file. Can be called if the template state is Loaded.
	/// Must not be called if the template file is already unloaded, or invalid.
	void unloadTemplateFile();
	
	
	/// Must draw the template using the given painter with the given opacity.
	/// The clip rect is in template coordinates,
	/// the scale is the combined view & template scale,
	/// which can be used to give a minimum size to elements.
	/// The painter transformation is set to use template coordinates.
    virtual void drawTemplate(QPainter* painter, QRectF& clip_rect, double scale, bool on_screen, float opacity) const = 0;
	
	/// Calculates the template's bounding box in map coordinates.
	virtual QRectF calculateTemplateBoundingBox() const;
	
	/// Returns the extent of the template out of the bounding box,
	/// which is defined in map coordinates, in pixels. This is useful for elements which
	/// stay the same size regardless of the zoom level, where a bounding box in map coords
	/// cannot be calculated.
	virtual int getTemplateBoundingBoxPixelBorder() {return 0;}
	
	/// Marks the whole area of the template as "to be redrawn".
	/// Use this before and after modifications to the template transformation.
	/// The default implementation marks everything as "to be redrawn" for georeferenced
	/// templates and uses the reported extent otherwise
	virtual void setTemplateAreaDirty();
	
	
	/// Must return if freehand drawing onto the template is possible
	virtual bool canBeDrawnOnto() const {return false;}
	
	/// Draws onto the template. coords is an array of points with which the
	/// drawn line is defined and must contain at least 2 points.
	/// map_bbox can be an invalid rect, then the method will calculate it itself.
	/// This only works for templates for which canBeDrawnOnto() returns true.
	void drawOntoTemplate(MapCoordF* coords, int num_coords, QColor color, float width, QRectF map_bbox);
	
	/// Triggers an undo or redo action for template freehand drawing.
	/// The type of action is determined by the parameter.
	/// This only works for templates for which canBeDrawnOnto() returns true.
	virtual void drawOntoTemplateUndo(bool redo);
	
	
	// Transformation related methods for non-georeferenced templates only
	
	/// Changes the painter's transformation so it can be used to draw in template coordinates.
	/// The previous transformation of the painter must be the map transformation.
	/// NOTE: for non-georeferenced templates only,
	/// or if the template transformation has been set by the template nevertheless.
	void applyTemplateTransform(QPainter* painter) const;
	
	/// Returns the extent of the template in template coordinates.
	/// The default implementation returns a "very big" rectangle.
	/// NOTE: for non-georeferenced templates only!
	virtual QRectF getTemplateExtent() const;
	
	/// Scales the template with the given scaling center.
	/// NOTE: for non-georeferenced templates only!
	void scale(double factor, const MapCoord& center);
	
	/// Rotates the template around the given point.
	/// NOTE: for non-georeferenced templates only!
	void rotate(double rotation, const MapCoord& center);
	
	
	// Coordinate transformations between template coordinates and map coordinates
	
	inline MapCoordF mapToTemplate(MapCoordF coords) const
	{
		return MapCoordF(map_to_template.get(0, 0) * coords.x() + map_to_template.get(0, 1) * coords.y() + map_to_template.get(0, 2),
		                 map_to_template.get(1, 0) * coords.x() + map_to_template.get(1, 1) * coords.y() + map_to_template.get(1, 2));
	}
	inline MapCoordF mapToTemplateOther(MapCoordF coords) const	// normally not needed - this uses the other transformation parameters
	{
		Q_ASSERT(!is_georeferenced);
		// SLOW - cache this matrix if needed often
		Matrix map_to_template_other;
		template_to_map_other.invert(map_to_template_other);
		return MapCoordF(map_to_template_other.get(0, 0) * coords.x() + map_to_template_other.get(0, 1) * coords.y() + map_to_template_other.get(0, 2),
		                 map_to_template_other.get(1, 0) * coords.x() + map_to_template_other.get(1, 1) * coords.y() + map_to_template_other.get(1, 2));
	}
	inline MapCoordF templateToMap(QPointF coords) const
	{
		return MapCoordF(template_to_map.get(0, 0) * coords.x() + template_to_map.get(0, 1) * coords.y() + template_to_map.get(0, 2),
		                 template_to_map.get(1, 0) * coords.x() + template_to_map.get(1, 1) * coords.y() + template_to_map.get(1, 2));
	}
	inline MapCoordF templateToMapOther(QPointF coords) const	// normally not needed - this uses the other transformation parameters
	{
		Q_ASSERT(!is_georeferenced);
		return MapCoordF(template_to_map_other.get(0, 0) * coords.x() + template_to_map_other.get(0, 1) * coords.y() + template_to_map_other.get(0, 2),
		                 template_to_map_other.get(1, 0) * coords.x() + template_to_map_other.get(1, 1) * coords.y() + template_to_map_other.get(1, 2));
	}
	
	
	// Pass points & adjustment
	
	inline const PassPointList& getPassPointList() const {return passpoints;}
	inline PassPointList& getPassPointList() {return passpoints;}
	inline int getNumPassPoints() const {return passpoints.size();}
	inline PassPoint* getPassPoint(int i) {return &passpoints[i];}
	void addPassPoint(const PassPoint& point, int pos);
	void deletePassPoint(int pos);
	void clearPassPoints();
	
	/// Change from adjusted into original state or the other way round
	void switchTransforms();
	void getTransform(TemplateTransform& out) const;
	void setTransform(const TemplateTransform& transform);
	void getOtherTransform(TemplateTransform& out) const;
	void setOtherTransform(const TemplateTransform& transform);
	
	
	// Getters / Setters
	// General
	
	inline Map* getMap() const {return map;}
	
	inline const QString& getTemplateFilename() const {return template_file;}
	
	inline const QString& getTemplatePath() const {return template_path;}
	/// Changes the path and filename only. Does not do any reloading etc.
	void setTemplatePath(const QString& value);
	
	inline const QString& getTemplateRelativePath() const {return template_relative_path;}
	inline void setTemplateRelativePath(const QString& value) {template_relative_path = value;}
	
	inline State getTemplateState() const {return template_state;}
	inline void setTemplateState(State state) {template_state = state;}
	
	inline int getTemplateGroup() const {return template_group;}
	inline void setTemplateGroup(int value) {template_group = value;}
	
	inline bool hasUnsavedChanges() const {return has_unsaved_changes;}
	void setHasUnsavedChanges(bool value);
	
	inline bool isTemplateGeoreferenced() const {return is_georeferenced;}
	inline void setTemplateGeoreferenced(bool value) {is_georeferenced = value;}
	
	// Transformation of non-georeferenced templates
	
	MapCoord templatePosition() const;
	void setTemplatePosition(MapCoord coord);
	
	inline qint64 getTemplateX() const {return transform.template_x;}
	inline void setTemplateX(qint64 x) {transform.template_x = x; updateTransformationMatrices();}
	
	inline qint64 getTemplateY() const {return transform.template_y;}
	inline void setTemplateY(qint64 y) {transform.template_y = y; updateTransformationMatrices();}
	
	inline double getTemplateScaleX() const {return transform.template_scale_x;}
	inline void setTemplateScaleX(double scale_x) {transform.template_scale_x = scale_x; updateTransformationMatrices();}
	inline double getTemplateScaleY() const {return transform.template_scale_y;}
	inline void setTemplateScaleY(double scale_y) {transform.template_scale_y = scale_y; updateTransformationMatrices();}
	
	inline double getTemplateRotation() const {return transform.template_rotation;}
	inline void setTemplateRotation(double rotation) {transform.template_rotation = rotation; updateTransformationMatrices();}
	
	inline bool isAdjustmentApplied() const {return adjusted;}
	inline bool isAdjustmentDirty() const {return adjustment_dirty;}
	void setAdjustmentDirty(bool value);
	
	// Static
	/**
	 * Returns the filename extensions supported by known subclasses.
	 */
	static const std::vector<QByteArray>& supportedExtensions();
	
	/**
	 * Creates a Template instance for the given path.
	 * 
	 * This function tries to find a matching template subclass by looking at
	 * the file extension. It may return nullptr if no subclass supports the
	 * extension.
	 */
	static std::unique_ptr<Template> templateForFile(const QString& path, Map* map);
	
signals:
	/// Emitted whenever template_state was changed
	void templateStateChanged();
	
protected:
	/**
	 * Sets the error description which will be returned by errorString().
	 */
	void setErrorString(const QString &text);
	
	/// Derived classes must create a duplicate and transfer
	/// type specific information over to the copy here.
	/// This includes the content of the template file if it is loaded.
	virtual Template* duplicateImpl() const = 0;
	
	/// Derived classes must load type specific template parameters here and return true if successful
	virtual bool loadTypeSpecificTemplateConfiguration(QIODevice* stream, int version);
	
	/// Derived classes must save type specific template parameters here
	virtual void saveTypeSpecificTemplateConfiguration(QXmlStreamWriter& xml) const;
	
	/// Derived classes must load type specific template parameters here and return false
	/// if a critical error ocurrs and loading must be aborted.
	/// This method is called for every xml tag under the template tag which is not parsed by
	/// the base class.
	/// IMPORTANT: implementations must call xml.skipCurrentElement() if they do not parse it.
	virtual bool loadTypeSpecificTemplateConfiguration(QXmlStreamReader& xml);
	
	/// Derived classes must load the template file here and return true if successful.
	/// If configuring is true, a call to postLoadConfiguration() will follow if this returns true.
	virtual bool loadTemplateFileImpl(bool configuring) = 0;
	
	/// Derived classes must unload the template file here
	virtual void unloadTemplateFileImpl() = 0;
	
	
	/// Must be implemented to draw the polyline given by the points onto the template if canBeDrawnOnto() returns true
	virtual void drawOntoTemplateImpl(MapCoordF* coords, int num_coords, QColor color, float width);
	
	
	/// Must be called after direct changes to transform or other_transform
	void updateTransformationMatrices();
	
	
	// General properties
	
	/// Map containing this template
	Map* map;
	
	/// The filename of the template file (e.g. "map.bmp")
	QString template_file;
	
	/// The complete path to the template file including the filename (e.g. "/home/me/map.bmp")
	QString template_path;
	
	/// The template path relative to the map file (e.g. "../me/map.bmp").
	/// Can be empty as long as the map file has not been saved yet.
	QString template_relative_path;
	
	/// The template lifetime state
	State template_state;
	
	/// The description of the last error
	QString error_string;
	
	/// Does the template itself (not its transformation) have unsaved changes (e.g. GPS track has changed, image has been painted on)
	bool has_unsaved_changes;
	
	/// Is the template in georeferenced mode?
	bool is_georeferenced;
	
	
	// Properties for non-georeferenced templates (invalid if is_georeferenced is true) 
	
	/// Currently active transformation. NOTE: after direct changes here call updateTransformationMatrices()
	TemplateTransform transform;
	
	/// Currently inactive transformation (adjusted or original). NOTE: after direct changes here call updateTransformationMatrices()
	TemplateTransform other_transform;
	
	/// If true, transform is the adjusted transformation, otherwise it is the original one
	bool adjusted;
	
	/// If true, the adjusted transformation has to be recalculated
	bool adjustment_dirty;
	
	/// List of pass points for position adjustment
	PassPointList passpoints;
	
	/// Number of the template group. If the template is not grouped, this is set to -1.
	int template_group;
	
	// Transformation matrices calculated from cur_trans
	Matrix map_to_template;
	Matrix template_to_map;
	Matrix template_to_map_other;
};



// ### Template inline code ###





#endif
