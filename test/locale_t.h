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

#ifndef OPENORIENTEERING_LOCALE_T_H
#define OPENORIENTEERING_LOCALE_T_H

#include <QtTest/QtTest>


/**
 * @test Tests locale and translation issues.
 */
class LocaleTest : public QObject
{
Q_OBJECT
public:
	explicit LocaleTest(QObject* parent = nullptr);
	
private slots:
	/**
	 * Tests handling of Esperanto in QLocale.
	 */
	void testEsperantoQLocale();
	
	/**
	 * Tests handling of Esperanto in TranslationUtil.
	 */
	void testEsperantoTranslationUtil();
	
};

#endif
