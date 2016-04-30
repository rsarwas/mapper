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


#include "color_dock_widget.h"

#include <QApplication>
#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QToolButton>

#include "core/map_color.h"
#include "map.h"
#include "util.h"
#include "util_gui.h"
#include "util/item_delegates.h"
#include "gui/color_dialog.h"
#include "gui/main_window.h"
#include "gui/widgets/segmented_button_layout.h"

ColorWidget::ColorWidget(Map* map, MainWindow* window, QWidget* parent)
: QWidget(parent), 
  map(map), 
  window(window)
{
	react_to_changes = true;
	
	setWhatsThis(Util::makeWhatThis("color_dock_widget.html"));
	
	// Color table
	color_table = new QTableWidget(map->getNumColors(), 7);
	color_table->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::AnyKeyPressed);
	color_table->setSelectionMode(QAbstractItemView::SingleSelection);
	color_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	color_table->verticalHeader()->setVisible(false);
	color_table->setHorizontalHeaderLabels(QStringList() <<
	  QString{} << tr("Name") << tr("Spot color") << tr("CMYK") << tr("RGB") << tr("K.o.") << tr("Opacity") );
	color_table->setItemDelegateForColumn(0, new ColorItemDelegate(this));
	color_table->setItemDelegateForColumn(6, new PercentageDelegate(this));
	color_table->setColumnHidden(6, true);
	
	QMenu* new_button_menu = new QMenu(this);
	(void) new_button_menu->addAction(tr("New"), this, SLOT(newColor()));
	duplicate_action = new_button_menu->addAction(tr("Duplicate"), this, SLOT(duplicateColor()));
	duplicate_action->setIcon(QIcon(QString::fromLatin1(":/images/tool-duplicate.png")));
	
	// Buttons
	QToolButton* new_button = newToolButton(QIcon(QString::fromLatin1(":/images/plus.png")), tr("New"));
	new_button->setPopupMode(QToolButton::DelayedPopup); // or MenuButtonPopup
	new_button->setMenu(new_button_menu);
	delete_button = newToolButton(QIcon(QString::fromLatin1(":/images/minus.png")), tr("Delete"));
	
	SegmentedButtonLayout* add_remove_layout = new SegmentedButtonLayout();
	add_remove_layout->addWidget(new_button);
	add_remove_layout->addWidget(delete_button);
	
	move_up_button = newToolButton(QIcon(QString::fromLatin1(":/images/arrow-up.png")), tr("Move Up"));
	move_up_button->setAutoRepeat(true);
	move_down_button = newToolButton(QIcon(QString::fromLatin1(":/images/arrow-down.png")), tr("Move Down"));
	move_down_button->setAutoRepeat(true);
	
	SegmentedButtonLayout* up_down_layout = new SegmentedButtonLayout();
	up_down_layout->addWidget(move_up_button);
	up_down_layout->addWidget(move_down_button);
	
	// TODO: In Mapper >= 0.6, switch to ColorWidget (or generic) translation context.
	edit_button = newToolButton(QIcon(QString::fromLatin1(":/images/settings.png")), QApplication::translate("MapEditorController", "&Edit").remove(QLatin1Char('&')));
	edit_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	
	QToolButton* help_button = newToolButton(QIcon(QString::fromLatin1(":/images/help.png")), tr("Help"));
	help_button->setAutoRaise(true);
	
	// The buttons row layout
	QBoxLayout* buttons_group_layout = new QHBoxLayout();
	buttons_group_layout->addLayout(add_remove_layout);
	buttons_group_layout->addLayout(up_down_layout);
	buttons_group_layout->addWidget(edit_button);
	buttons_group_layout->addWidget(new QLabel(QString::fromLatin1("   ")), 1);
	buttons_group_layout->addWidget(help_button);
	
	// The layout of all components below the table
	QBoxLayout* bottom_layout = new QVBoxLayout();
	QStyleOption style_option(QStyleOption::Version, QStyleOption::SO_DockWidget);
	bottom_layout->setContentsMargins(
		style()->pixelMetric(QStyle::PM_LayoutLeftMargin, &style_option) / 2,
		0, // Covered by the main layout's spacing.
		style()->pixelMetric(QStyle::PM_LayoutRightMargin, &style_option) / 2,
		style()->pixelMetric(QStyle::PM_LayoutBottomMargin, &style_option) / 2
	);
	bottom_layout->addLayout(buttons_group_layout);
	bottom_layout->addWidget(new QLabel(tr("Double-click a color value to open a dialog.")));
	
	// The main layout
	QBoxLayout* layout = new QVBoxLayout();
	layout->setContentsMargins(QMargins());
	layout->addWidget(color_table, 1);
	layout->addLayout(bottom_layout);
	setLayout(layout);
	
	for (int i = 0; i < map->getNumColors(); ++i)
		addRow(i);
	
	QHeaderView* header_view = color_table->horizontalHeader();
	header_view->setSectionResizeMode(QHeaderView::Interactive);
	header_view->resizeSections(QHeaderView::ResizeToContents);
	header_view->setSectionResizeMode(0, QHeaderView::Fixed); // Color
	header_view->resizeSection(0, 32);
	header_view->setSectionResizeMode(1, QHeaderView::Stretch); // Name
	header_view->setSectionResizeMode(1, QHeaderView::Interactive); // Spot colors
	header_view->setSectionResizeMode(5, QHeaderView::Fixed); // Knockout
	header_view->resizeSection(5, 32);
	header_view->setSectionsClickable(false);
	
	currentCellChange(color_table->currentRow(), 0, 0, 0);	// enable / disable move color buttons
	
	// Connections
	connect(color_table, SIGNAL(cellChanged(int,int)), this, SLOT(cellChange(int,int)));
	connect(color_table, SIGNAL(currentCellChanged(int,int,int,int)), this, SLOT(currentCellChange(int,int,int,int)));
	connect(color_table, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(editCurrentColor()));
	
	connect(new_button, SIGNAL(clicked(bool)), this, SLOT(newColor()));
	connect(delete_button, SIGNAL(clicked(bool)), this, SLOT(deleteColor()));
	connect(move_up_button, SIGNAL(clicked(bool)), this, SLOT(moveColorUp()));
	connect(move_down_button, SIGNAL(clicked(bool)), this, SLOT(moveColorDown()));
	connect(edit_button, SIGNAL(clicked(bool)), this, SLOT(editCurrentColor()));
	connect(help_button, SIGNAL(clicked(bool)), this, SLOT(showHelp()));
	
	connect(map, SIGNAL(colorAdded(int, const MapColor*)), this, SLOT(colorAdded(int, const MapColor*)));
	connect(map, SIGNAL(colorChanged(int, const MapColor*)), this, SLOT(colorChanged(int, const MapColor*)));
	connect(map, SIGNAL(colorDeleted(int, const MapColor*)), this, SLOT(colorDeleted(int, const MapColor*)));
}

ColorWidget::~ColorWidget()
{
}

QToolButton* ColorWidget::newToolButton(const QIcon& icon, const QString& text)
{
	QToolButton* button = new QToolButton();
	button->setToolButtonStyle(Qt::ToolButtonFollowStyle);
	button->setToolTip(text);
	button->setIcon(icon);
	button->setText(text);
	button->setWhatsThis(whatsThis());
	return button;
}

void ColorWidget::newColor()
{
	int row = color_table->currentRow();
	if (row < 0)
		row = color_table->rowCount();
	map->addColor(new MapColor(), row);
	
	map->updateAllObjects();
	
	editCurrentColor();
}

void ColorWidget::deleteColor()
{
	int row = color_table->currentRow();
	Q_ASSERT(row >= 0);
	if (row < 0) return; // In release mode
	
	// Show a warning if the color is used
	if (map->isColorUsedByASymbol(map->getColor(row)))
	{
		if (QMessageBox::warning(this, tr("Confirmation"), tr("The map contains symbols with this color. Deleting it will remove the color from these objects! Do you really want to do that?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
			return;
	}
	
	map->deleteColor(row);
	
	map->setColorsDirty();
	map->updateAllObjects();
}

void ColorWidget::duplicateColor()
{
	int row = color_table->currentRow();
	Q_ASSERT(row >= 0);
	if (row < 0) return; // In release mode
	
	MapColor* new_color = new MapColor(*map->getColor(row));
	new_color->setName(new_color->getName() + tr(" (Duplicate)"));
	map->addColor(new_color, row);
	
	map->updateAllObjects();
	
	editCurrentColor();
}

void ColorWidget::moveColorUp()
{
	int row = color_table->currentRow();
	Q_ASSERT(row >= 1);
	if (row < 1) return; // In release mode
	
	MapColor* above_color = map->getMapColor(row - 1);
	MapColor* cur_color = map->getMapColor(row);
	map->setColor(cur_color, row - 1);
	map->setColor(above_color, row);
	updateRow(row - 1);
	updateRow(row);
	
	color_table->setCurrentCell(row - 1, color_table->currentColumn());
	
	map->setColorsDirty();
	map->updateAllObjects();
}

void ColorWidget::moveColorDown()
{
	int row = color_table->currentRow();
	Q_ASSERT(row < color_table->rowCount() - 1);
	if (row >= color_table->rowCount() - 1) return; // In release mode
	
	MapColor* below_color = map->getMapColor(row + 1);
	MapColor* cur_color = map->getMapColor(row);
	map->setColor(cur_color, row + 1);
	map->setColor(below_color, row);
	updateRow(row + 1);
	updateRow(row);
	
	color_table->setCurrentCell(row + 1, color_table->currentColumn());
	
	map->setColorsDirty();
	map->updateAllObjects();
}

// slot
void ColorWidget::editCurrentColor()
{
	int row = color_table->currentRow();
	if (row >= 0)
	{
		MapColor* color = map->getMapColor(row);
		ColorDialog dialog(*map, *color, this);
		dialog.setWindowModality(Qt::WindowModal);
		int result = dialog.exec();
		if (result == QDialog::Accepted)
		{
			*color = dialog.getColor();
			map->setColor(color, row); // trigger colorChanged signal
			map->setColorsDirty();
			map->updateAllObjects();
		}
	}
}

void ColorWidget::showHelp() const
{
	Util::showHelp(window, "color_dock_widget.html", "");
}

void ColorWidget::cellChange(int row, int column)
{
	if (!react_to_changes)
		return;
	
	react_to_changes = false;
	
	MapColor* color = map->getMapColor(row);
	QString text = color_table->item(row, column)->text().trimmed();
	
	if (column == 1)
	{
		color->setName(text);
		react_to_changes = true;
	}
	else if (column == 6) // Opacity
	{
		auto opacity = color_table->item(row, column)->data(Qt::DisplayRole).toFloat();
		if (!qFuzzyCompare(1.0f+opacity, 1.0f+color->getOpacity()))
		{
			color->setOpacity(qBound(0.0f, opacity, 1.0f));
			updateRow(row);
		}
		react_to_changes = true;
	}
	else 
	{
		react_to_changes = true;
		return;
	}
	
	map->setColor(color, row); // trigger colorChanged signal
	map->setColorsDirty();
	map->updateAllObjects();
}

void ColorWidget::currentCellChange(int current_row, int current_column, int previous_row, int previous_column)
{
	Q_UNUSED(current_column);
	Q_UNUSED(previous_row);
	Q_UNUSED(previous_column);
	if (!react_to_changes)
		return;
	
	bool valid_row = (current_row >= 0);
	delete_button->setEnabled(valid_row);
	duplicate_action->setEnabled(valid_row);
	move_up_button->setEnabled(valid_row && current_row >= 1);
	move_down_button->setEnabled(valid_row && current_row < color_table->rowCount() - 1);
	edit_button->setEnabled(valid_row);
}

void ColorWidget::colorAdded(int index, const MapColor* color)
{
	Q_UNUSED(color);
	color_table->insertRow(index);
	addRow(index);
	if (index < color_table->rowCount() - 1)
	{
		updateRow(index + 1);
	}
	color_table->setCurrentCell(index, 1);
}

void ColorWidget::colorChanged(int index, const MapColor* color)
{
	Q_UNUSED(color);
	updateRow(index);
}

void ColorWidget::colorDeleted(int index, const MapColor* color)
{
	Q_UNUSED(color);
	color_table->removeRow(index);
	currentCellChange(color_table->currentRow(), color_table->currentColumn(), -1, -1);
}

void ColorWidget::addRow(int row)
{
	react_to_changes = false;
	
	QTableWidgetItem* item;
	for (int col = 0; col < color_table->columnCount(); ++col)
	{
		item = new QTableWidgetItem();
		item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		// TODO: replace "define" with "edit"
		item->setToolTip(tr("Double click to define the color"));
		color_table->setItem(row, col, item);
	}
	
	// Name
	item = color_table->item(row, 1);
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
	item->setToolTip(tr("Click to select the name and click again to edit."));
	
	react_to_changes = true;
	
	// Opacity
	item = color_table->item(row, 6);
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
	
	updateRow(row);
}

void ColorWidget::updateRow(int row)
{
	react_to_changes = false;
	
	const MapColor* color = map->getColor(row);
	
	// Color preview
	QTableWidgetItem* item = color_table->item(row, 0);
	item->setBackground((const QColor&) *color);
	
	// Name
	item = color_table->item(row, 1);
	item->setText(color->getName());
	
	// Spot color
	item = color_table->item(row, 2);
	item->setText(color->getSpotColorName());
	switch (color->getSpotColorMethod())
	{
		case MapColor::SpotColor:
			item->setData(Qt::DecorationRole, (const QColor&)*color);
			break;
		default:
			item->setData(Qt::DecorationRole, QColor(Qt::transparent));
	}
	
	// CMYK
	item = color_table->item(row, 3);
	item->setToolTip(tr("Double click to define the color"));
	const MapColorCmyk& cmyk = color->getCmyk();
	QLocale l;
	item->setText(QString::fromLatin1("%1/%2/%3/%4").arg(
	                l.toString(100*cmyk.c, 'g', 3), l.toString(100*cmyk.m, 'g', 3),
	                l.toString(100*cmyk.y, 'g', 3), l.toString(100*cmyk.k, 'g', 3)));
	switch (color->getCmykColorMethod())
	{
		case MapColor::SpotColor:
		case MapColor::RgbColor:
			item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
			item->setData(Qt::DecorationRole, QColor(Qt::transparent));
			break;
		default:
			item->setForeground(palette().color(QPalette::Active, QPalette::Text));
			item->setData(Qt::DecorationRole, (QColor)(color->getCmyk()));
	}
	
	// RGB
	item = color_table->item(row, 4);
	item->setText(QColor(color->getRgb()).name());
	item->setToolTip(item->text());
	switch (color->getRgbColorMethod())
	{
		case MapColor::SpotColor:
		case MapColor::CmykColor:
			item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
			item->setData(Qt::DecorationRole, QColor(Qt::transparent));
			break;
		default:
			item->setForeground(palette().color(QPalette::Active, QPalette::Text));
			item->setData(Qt::DecorationRole, (const QColor&)(color->getRgb()));
	}
	
	// Knockout
	item = color_table->item(row, 5);
	item->setCheckState(color->getKnockout() ? Qt::Checked : Qt::Unchecked);
	item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
	
	// Opacity
	item = color_table->item(row, 6);
	item->setData(Qt::DisplayRole, color->getOpacity());
	
	react_to_changes = true;
}

