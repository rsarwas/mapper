/*
 *    Copyright 2012, 2013 Thomas Schöps, Kai Pastor
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


#include "home_screen_controller.h"

#include <QFile>
#include <QStatusBar>

#include "main_window.h"
#include "widgets/home_screen_widget.h"
#include "../settings.h"


HomeScreenController::HomeScreenController()
: widget(NULL),
  current_tip(-1)
{
	// nothing
}

HomeScreenController::~HomeScreenController()
{
	// nothing
}

void HomeScreenController::attach(MainWindow* window)
{
	this->window = window;
	
	if (window->mobileMode())
	{
		widget = new HomeScreenWidgetMobile(this);
	}
	else
	{
		widget = new HomeScreenWidgetDesktop(this);
		window->statusBar()->hide();
		window->setStatusBarText(QString{});
	}
	
	window->setCentralWidget(widget);
	
	connect(&Settings::getInstance(), SIGNAL(settingsChanged()), this, SLOT(readSettings()));
	
	readSettings(true);
}

void HomeScreenController::detach()
{
	if (!window->mobileMode())
	{
		window->statusBar()->show();
	}
	window->setCentralWidget(NULL);
	widget->deleteLater();
	
	Settings::getInstance().setSetting(Settings::HomeScreen_CurrentTip, current_tip);
}

void HomeScreenController::readSettings(bool init_current_tip)
{
	Settings& settings = Settings::getInstance(); // FIXME: settings should be const
	
	widget->setRecentFiles(settings.getSettingCached(Settings::General_RecentFilesList).toStringList());
	widget->setOpenMRUFileChecked(settings.getSettingCached(Settings::General_OpenMRUFile).toBool());
	
	bool tips_visible = settings.getSettingCached(Settings::HomeScreen_TipsVisible).toBool();
	widget->setTipsVisible(tips_visible);
	if (init_current_tip)
	{
		// The home screen becomes active.
		current_tip = settings.getSettingCached(Settings::HomeScreen_CurrentTip).toInt();
		if (tips_visible)
			goToNextTip();
	}
	else if (tips_visible)
		// Settings changed.
		goToTip(current_tip);
}

void HomeScreenController::setOpenMRUFile(bool state)
{
	Settings::getInstance().setSetting(Settings::General_OpenMRUFile, state);
}

void HomeScreenController::clearRecentFiles()
{
	Settings::getInstance().remove(Settings::General_RecentFilesList);
}

void HomeScreenController::setTipsVisible(bool state)
{
	Settings::getInstance().setSetting(Settings::HomeScreen_TipsVisible, state);
}

void HomeScreenController::goToNextTip()
{
	goToTip(current_tip + 1);
}

void HomeScreenController::goToPreviousTip()
{
	goToTip(current_tip - 1);
}

void HomeScreenController::goToTip(int index)
{
	static QStringList tips;
	if (tips.isEmpty())
	{
		// Normally, this will be read only once.
		QFile file(QString::fromLatin1(":/help/tip-of-the-day/tips.txt"));
		if (file.open(QIODevice::ReadOnly))
		{
			while (!file.atEnd())
			{
				QString tip(QString::fromUtf8(file.readLine().constData()));
				if (tip.endsWith(QLatin1Char('\n')))
					tip.chop(1);
				if (!tip.isEmpty())
					tips.push_back(tip);
			}
		}
	}
	
	if (tips.isEmpty())
	{
		// Some error may have occured during reading the tips file.
		// Display a welcome text.
		widget->setTipOfTheDay(QString::fromLatin1("<h2>%1</h2>").arg(tr("Welcome to OpenOrienteering Mapper!")));
	}
	else
	{
		Q_ASSERT(tips.count() > 0);
		while (index < 0)
			index += tips.count();
		current_tip = index % tips.count();
		widget->setTipOfTheDay(tips[current_tip]);
	}
}
