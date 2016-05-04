/*
 *      tm_parser.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2016 Colomban Wendling <ban(at)herbesfolles(dot)org>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License along
 *      with this program; if not, write to the Free Software Foundation, Inc.,
 *      51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "tm_parser.h"
#include "tm_ctags_wrappers.h"


int main(void)
{
	tm_ctags_init();
	return tm_parser_verify_type_mappings() ? 0 : 1;
}
