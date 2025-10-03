/*   GIMP - The GNU Image Manipulation Program
 *   Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 *   Dpx Image Format plug-in
 *
 *   Copyright (C) 2023 Alex S.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "config.h"

#include <string.h>
#include <errno.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define LOAD_PROC      "file-dpx-load"
#define PLUG_IN_BINARY "file-dpx"
#define PLUG_IN_ROLE   "gimp-file-dpx"


typedef struct _Dpx      Dpx;
typedef struct _DpxClass DpxClass;

struct _Dpx
{
  GimpPlugIn      parent_instance;
};

struct _DpxClass
{
  GimpPlugInClass parent_class;
};


#define DPX_TYPE  (dpx_get_type ())
#define DPX(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DPX_TYPE, Dpx))

GType                   dpx_get_type         (void) G_GNUC_CONST;


static GList          * dpx_query_procedures (GimpPlugIn            *plug_in);
static GimpProcedure  * dpx_create_procedure (GimpPlugIn            *plug_in,
                                              const gchar           *name);

static GimpValueArray * dpx_load             (GimpProcedure         *procedure,
                                              GimpRunMode            run_mode,
                                              GFile                 *file,
                                              GimpMetadata          *metadata,
                                              GimpMetadataLoadFlags *flags,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);

static GimpImage      * load_image            (GFile                 *file,
                                               GObject               *config,
                                               GimpRunMode            run_mode,
                                               GError               **error);

G_DEFINE_TYPE (Dpx, dpx, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (DPX_TYPE)
DEFINE_STD_SET_I18N


static void
dpx_class_init (DpxClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = dpx_query_procedures;
  plug_in_class->create_procedure = dpx_create_procedure;
  plug_in_class->set_i18n         = STD_SET_I18N;
}

static void
dpx_init (Dpx *dpx)
{
}

static GList *
dpx_query_procedures (GimpPlugIn *plug_in)
{
  GList *list = NULL;

  list = g_list_append (list, g_strdup (LOAD_PROC));

  return list;
}

static GimpProcedure *
dpx_create_procedure (GimpPlugIn  *plug_in,
                      const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (! strcmp (name, LOAD_PROC))
    {
      procedure = gimp_load_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           dpx_load, NULL, NULL);

      gimp_procedure_set_menu_label (procedure,
                                     _("DPX"));

      gimp_procedure_set_documentation (procedure,
                                        _("Load file in the Dpx file "
                                          "format"),
                                        _("Load file in the Dpx file "
                                          "format"),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Alex S.",
                                      "Alex S.",
                                      "2023");

      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "dpx");
      gimp_file_procedure_set_magics (GIMP_FILE_PROCEDURE (procedure),
                                      "0,string,SDPX");
    }
  return procedure;
}

static GimpValueArray *
dpx_load (GimpProcedure         *procedure,
          GimpRunMode            run_mode,
          GFile                 *file,
          GimpMetadata          *metadata,
          GimpMetadataLoadFlags *flags,
          GimpProcedureConfig   *config,
          gpointer               run_data)
{
  GimpValueArray *return_vals;
  GimpImage      *image;
  GError         *error = NULL;

  gegl_init (NULL, NULL);

  image = load_image (file, G_OBJECT (config), run_mode, &error);

  if (! image)
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR,
                                             error);

  return_vals = gimp_procedure_new_return_values (procedure,
                                                  GIMP_PDB_SUCCESS,
                                                  NULL);

  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}

static GimpImage *
load_image (GFile        *file,
            GObject      *config,
            GimpRunMode   run_mode,
            GError      **error)
{
  GimpImage  *image  = NULL;
  GimpLayer  *layer;
  GeglBuffer *buffer;
  guint16    *pixels;
  guchar      magic_number[4];
  guint32     width;
  guint32     height;
  gsize       row_size;
  const Babl *format = babl_format ("R'G'B'A u16");
  FILE       *fp;

  fp = g_fopen (g_file_peek_path (file), "rb");

  if (! fp)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for reading: %s"),
                   gimp_file_get_utf8_name (file), g_strerror (errno));
      return NULL;
    }

  /* Load the header */
  if (! fread (magic_number, 4, 1, fp))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Failed to read Dpx header"));
      fclose (fp);
      return NULL;
    }

  /*if (fseek(fp, 772, SEEK_SET) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Failed to seek to Dpx image dimensions"));
      fclose (fp);
      return NULL;
    }
  if (! fread (&width, sizeof (guint32), 1, fp) ||
      ! fread (&height, sizeof (guint32), 1, fp))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Failed to read Dpx image dimensions"));
      fclose (fp);
      return NULL;
    }

  width = GUINT32_FROM_BE (width);
  height = GUINT32_FROM_BE (height);

  if (width > GIMP_MAX_IMAGE_SIZE  ||
      height > GIMP_MAX_IMAGE_SIZE ||
      ! g_size_checked_mul (&row_size, width, (sizeof (guint16) * 4)))
    {
      g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                   _("Image dimensions too large: width %d x height %d"),
                   width, height);
      fclose (fp);
      return NULL;
    }

  image = gimp_image_new_with_precision (width, height, GIMP_RGB,
                                         GIMP_PRECISION_U16_NON_LINEAR);

  layer = gimp_layer_new (image, _("Background"), width, height,
                          GIMP_RGBA_IMAGE, 100,
                          gimp_image_get_default_new_layer_mode (image));
  gimp_image_insert_layer (image, layer, NULL, 0);

  pixels = g_try_malloc (row_size);
  if (pixels == NULL)
    {
      g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                   _("There was not enough memory to complete the "
                     "operation."));
      fclose (fp);
      return NULL;
    }

  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  for (gint i = 0; i < height; i++)
    {
      if (! fread (pixels, row_size, 1, fp))
        {
          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                       _("Premature end of Dpx pixel data"));
          return NULL;
        }

      for (gint j = 0; j < (width * 4); j++)
        pixels[j] = GUINT16_FROM_BE (pixels[j]);

      gegl_buffer_set (buffer,
                       GEGL_RECTANGLE (0, i, width, 1), 0,
                       format, pixels, GEGL_AUTO_ROWSTRIDE);
    }
  g_free (pixels);

  fclose (fp);
  g_object_unref (buffer);
*/
  return image;
}
