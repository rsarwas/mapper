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

#ifndef OPENORIENTEERING_OVERRIDING_SHORTCUT_H
#define OPENORIENTEERING_OVERRIDING_SHORTCUT_H

#include <Qt>
#include <QKeySequence>
#include <QObject>
#include <QShortcut>
#include <QTime>

class QEvent;
class QWidget;


/**
 * OverridingShortcut is a variation of QShortcut which takes precedence over
 * other listeners for the same key sequence.
 * 
 * It does so by reacting on events of type of QEvent::ShortcutOverride. Since
 * these events are of class QKeyEvent, the overriding works only for key
 * sequences consisting of a single key plus modifiers. For multi-key
 * sequences, the shortcut will work like a normal QShortcut.
 * 
 * OverridingShortcut now ignores a ShortcutOverride event if it occurs
 * immediately (within less than 50 milliseconds) after another. This works
 * around an issue which appeared in Qt 5.1.0 where the event filter receives
 * two ShortcutOverride events for a single shortcut key press.
 */
class OverridingShortcut : public QShortcut
{
Q_OBJECT
public:
	/**
	 * Constructs a OverridingShortcut object for the given parent widget.
	 * parent must not be nullptr.
	 * @see QShortcut::QShortcut(QWidget*)
	 */
	OverridingShortcut(QWidget* parent);
	
	/**
	 * Constructs a OverridingShortcut object.
	 * parent must not be nullptr.
	 * @see QShortcut::QShortcut(const QKeySequence&, QWidget*, const char*, const char*, Qt::ShortcutContext)
	 */
	OverridingShortcut(const QKeySequence& key, QWidget* parent, const char* member = 0, const char* ambiguousMember = 0, Qt::ShortcutContext context = Qt::WindowShortcut);
	
	/**
	 * Filters events of type QEvent::ShortcutOverride which match this
	 * shortcut's key sequence, and passes them as corresponding
	 * QShortcutEvent to QShortcut::event().
	 */
	bool eventFilter(QObject* watched, QEvent* event) override;
	
private:
	QTime time;
};

#endif
