/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#define _GNU_SOURCE
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "git-spot.h"
#include "cmd.h"

typedef void (*sg_callback) (void *user_data);

static void container_loaded(sp_playlistcontainer *pc, void *user_data);
static void save_playlist_async(sp_playlist *playlist, const char *directory,
    sg_callback cb, void *user_data);

typedef struct {
  sp_playlistcontainer *pc;
  int argc;
  char **argv;
  sp_playlistcontainer_callbacks *callbacks;
  int started_calls;
  int finished_calls;
} container_context;

static container_context *container_context_new(
    sp_playlistcontainer *pc,
    int argc,
    char **argv)
{
  container_context *ret = malloc(sizeof(container_context));
  ret->pc = pc;
  ret->argc = argc;
  ret->argv = argv;
  ret->callbacks = malloc(sizeof(sp_playlistcontainer_callbacks));
  memset(ret->callbacks, 0, sizeof(sp_playlistcontainer_callbacks));
  ret->started_calls = 0;
  ret->finished_calls = 0;
  return ret;
}

static void container_context_free(container_context *ctx) {
  sp_playlistcontainer_remove_callbacks(ctx->pc, ctx->callbacks, ctx);
  free(ctx->callbacks);
  free(ctx);
}

static int subscriptions_updated;

static void cmd_save_finally(container_context *ctx)
{
  cmd_logout(ctx->argc, ctx->argv);
  container_context_free(ctx);
}

static void container_context_start_call(container_context *ctx)
{
  ctx->started_calls ++;
}

static void container_context_finish_call(container_context *ctx)
{
  ctx->finished_calls ++;
  printf("%d of %d calls finished.\n", ctx->finished_calls, ctx->started_calls);
  if(ctx->finished_calls == ctx->started_calls)
    cmd_save_finally(ctx);
}

/**
 *
 */
int cmd_save(int argc, char **argv)
{
  sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);
  container_context *ctx = container_context_new(pc, argc, argv);
  ctx->callbacks->container_loaded = container_loaded;

  if (argc < 1)
    printf("this is going to be fun\n");

  sp_playlistcontainer_add_callbacks(pc, ctx->callbacks, ctx);
  return 1;
}

static void container_loaded(sp_playlistcontainer *pc, void *userdata)
{
  container_context *ctx = userdata;
  char **argv = ctx->argv;
  int i, j, level = 0;
  sp_playlist *pl;
  char name[200];

  container_context_start_call(ctx);
  printf("path = %s\n", argv[1]);
  printf("%d entries in the container\n", sp_playlistcontainer_num_playlists(pc));

  for (i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
    switch (sp_playlistcontainer_playlist_type(pc, i)) {
      case SP_PLAYLIST_TYPE_PLAYLIST:
        printf("%d. ", i);
        for (j = level; j; --j) printf("\t");
        pl = sp_playlistcontainer_playlist(pc, i);
        container_context_start_call(ctx);
        save_playlist_async(pl, "", (sg_callback)container_context_finish_call, ctx);
        printf("%s", sp_playlist_name(pl));
        if(subscriptions_updated)
          printf(" (%d subscribers)", sp_playlist_num_subscribers(pl));
        printf("\n");
        break;
      case SP_PLAYLIST_TYPE_START_FOLDER:
        printf("%d. ", i);
        for (j = level; j; --j) printf("\t");
        sp_playlistcontainer_playlist_folder_name(pc, i, name, sizeof(name));
        printf("Folder: %s with id %lu\n", name,
             sp_playlistcontainer_playlist_folder_id(pc, i));
        level++;
        break;
      case SP_PLAYLIST_TYPE_END_FOLDER:
        level--;
         printf("%d. ", i);
        for (j = level; j; --j) printf("\t");
        printf("End folder with id %lu\n",  sp_playlistcontainer_playlist_folder_id(pc, i));
        break;
      case SP_PLAYLIST_TYPE_PLACEHOLDER:
        printf("%d. Placeholder", i);
        break;
    }
  }

  printf("Made %d async calls.\n", ctx->started_calls -1);
  container_context_finish_call(ctx);
}

typedef struct {
  sp_playlist *playlist;
  char *directory;
  sg_callback cb;
  void *user_data;
  sp_playlist_callbacks *callbacks;
} playlist_data;

static playlist_data *playlist_data_new(sp_playlist *playlist,
    const char *directory,
    sg_callback cb,
    void *user_data)
{
  playlist_data *data = malloc(sizeof(playlist_data));

  data->playlist = playlist;
  data->directory = strdup(directory);
  data->cb = cb;
  data->user_data = user_data;
  data->callbacks = malloc(sizeof(sp_playlist_callbacks));
  memset(data->callbacks, 0, sizeof(sp_playlist_callbacks));

  return data;
}

static void playlist_data_free(playlist_data *data)
{
  sp_playlist_remove_callbacks(data->playlist, data->callbacks, data);
  free(data->directory);
  free(data->callbacks);
  free(data);
}

static void save_playlist_finally(playlist_data *data)
{
  if(data->cb != NULL)
    data->cb(data->user_data);
  playlist_data_free(data);
}

static void actually_save_playlist(playlist_data *data)
{
  int i;
  char *filename = strdup(sp_playlist_name(data->playlist));
  char *c;
  FILE *output;

  printf("Playlist '%s' ready.\n", sp_playlist_name(data->playlist));

  for(c=filename; *c != 0; c++)
    {
      if(strchr("/\\. \t+$'\"", *c) != NULL)
        *c = '_';
    }
  output = fopen(filename, "w+");
  free(filename);

  fprintf(output, "{'playlist_name': '%s', 'songs': [\n",
      sp_playlist_name(data->playlist));

  for(i=0; i<sp_playlist_num_tracks(data->playlist); i++)
    {
      sp_track *track = sp_playlist_track(data->playlist, i);
      sp_link *link = sp_link_create_from_track(track, 0);
      int j = 0;
      char link_str[100];
      char *artists_str = NULL;
      sp_album *album;
      const char *album_str = NULL;

      if(!sp_link_as_string(link, link_str, 100))
        printf("WARNING: sp_link_as_string failed.\n");

      for(j=0; j < sp_track_num_artists(track); j++)
        {
          char *new_artists_str = NULL;
          sp_artist *artist = sp_track_artist(track, j);
          if (-1 == asprintf(&new_artists_str, "%s, '%s'",
              artists_str, sp_artist_name(artist)))
            {
              printf("WARNING: asprinf failed.\n");
              break;
            }
          free(artists_str);
          artists_str = new_artists_str;
        }
      if (artists_str == NULL)
        artists_str = "(null), 'Dunno yet.'";

      album = sp_track_album(track);
      if(album != NULL && sp_album_is_loaded(album))
        album_str = sp_album_name(album);
      else
        album_str = "Dunno yet.";

      fprintf(output, "{'name': '%s', 'artists': [%s], 'album': '%s', "
          "'duration': %d, 'link': '%s'},\n",
          sp_track_name(track), artists_str + 8 /* strip off "(null), "*/,
          album_str, sp_track_duration(track), link_str);
    }

  fprintf(output, "]}\n");
  fclose(output);
  save_playlist_finally(data);
}

static void playlist_state_changed_cb(sp_playlist *pl, void *userdata)
{
  playlist_data *data = userdata;
  if (sp_playlist_is_loaded(data->playlist))
    actually_save_playlist(data);
}

static void save_playlist_async(sp_playlist *playlist,
    const char *directory,
    sg_callback cb,
    void *user_data)
{
  playlist_data *data = playlist_data_new(playlist, directory, cb, user_data);
  data->callbacks->playlist_state_changed = playlist_state_changed_cb;

  sp_playlist_add_callbacks(data->playlist, data->callbacks, data);
}


/**
 *
 */
int cmd_load(int argc, char **argv)
{
  int index, i;
  sp_track *track;
  sp_playlist *playlist;
  sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);

  if (argc < 1) {
    printf("playlist [playlist index]\n");
    return 0;
  }

  index = atoi(argv[1]);
  if (index < 0 || index > sp_playlistcontainer_num_playlists(pc)) {
    printf("invalid index\n");
    return 0;
  }
  playlist = sp_playlistcontainer_playlist(pc, index);
  printf("Playlist %s by %s%s%s\n",
       sp_playlist_name(playlist),
       sp_user_display_name(sp_playlist_owner(playlist)),
       sp_playlist_is_collaborative(playlist) ? " (collaborative)" : "",
       sp_playlist_has_pending_changes(playlist) ? " with pending changes" : ""
       );
  for (i = 0; i < sp_playlist_num_tracks(playlist); ++i) {
    track = sp_playlist_track(playlist, i);
    printf("%d. %c %s%s %s\n", i,
         sp_track_is_starred(g_session, track) ? '*' : ' ',
         sp_track_is_local(g_session, track) ? "local" : "     ",
         sp_track_is_autolinked(g_session, track) ? "autolinked" : "          ",
         sp_track_name(track));
  }
  return 1;
}

