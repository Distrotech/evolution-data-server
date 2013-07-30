/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "cursor-example.h"

int
main (int argc, char *argv[])
{
  GtkWidget *example;

  gtk_init (&argc, &argv);

  if (argc < 2)
    example = cursor_example_new ("./data");
  else
    example = cursor_example_new (argv[1]);

  g_signal_connect (example, "destroy",
		    G_CALLBACK (gtk_main_quit), NULL);

  gtk_widget_show (example);
  gtk_main ();

  return 0;
}
