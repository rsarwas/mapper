/*
 *    Copyright 2013 Kai Pastor
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

#include "overriding_shortcut.h"

#include <QtGlobal>
#include <QEvent>
#include <QKeyEvent>
#include <QShortcutEvent>
#include <QWidget>


namespace OpenOrienteering {

// ### OverridingShortcut ###

OverridingShortcut::OverridingShortcut(QWidget* parent)
 : QShortcut(parent)
{
	Q_ASSERT(parent);
	parent->window()->installEventFilter(this);
	timer.start();
}

OverridingShortcut::OverridingShortcut(const QKeySequence& key, QWidget* parent, const char* member, const char* ambiguousMember, Qt::ShortcutContext context)
 : QShortcut(key, parent, member, ambiguousMember, context)
{
	Q_ASSERT(parent);
	parent->window()->installEventFilter(this);
	timer.start();
}


OverridingShortcut::~OverridingShortcut() = default;



bool OverridingShortcut::eventFilter(QObject* /*watched*/, QEvent* event)
{
	if (event->type() == QEvent::ShortcutOverride && key().count() == 1)
	{
		QKeyEvent* key_event = static_cast<QKeyEvent*>(event);
		if ((key_event->key() | int(key_event->modifiers())) == key()[0])
		{
			if (timer.elapsed() < 50) // milliseconds
			{
				event->accept();
				return true;
			}
			
			QShortcutEvent se(key(), id());
			event->setAccepted(QShortcut::event(&se));
			timer.restart();
			return event->isAccepted();
		}
	}
	
	return false;
}


}  // namespace OpenOrienteering
