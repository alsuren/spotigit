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

#include <string.h>

#include "git-spot.h"
#include "cmd.h"

typedef struct {
  sp_playlistcontainer *pc;
  int argc;
  char **argv;
  sp_playlistcontainer_callbacks *callbacks;
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
  return ret;
}

static void container_context_free(container_context *ctx) {
  sp_playlistcontainer_remove_callbacks(ctx->pc, ctx->callbacks, ctx);
  free(ctx->callbacks);
  free(ctx);
}

static int subscriptions_updated;

static void container_loaded(sp_playlistcontainer *pc, void *userdata);

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
  int argc = ctx->argc;
  char **argv = ctx->argv;
	int i, j, level = 0;
	sp_playlist *pl;
	char name[200];

  printf("path = %s\n", argv[1]);
	printf("%d entries in the container\n", sp_playlistcontainer_num_playlists(pc));

	for (i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
		switch (sp_playlistcontainer_playlist_type(pc, i)) {
			case SP_PLAYLIST_TYPE_PLAYLIST:
				printf("%d. ", i);
				for (j = level; j; --j) printf("\t");
				pl = sp_playlistcontainer_playlist(pc, i);
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
				printf("End folder with id %lu\n",	sp_playlistcontainer_playlist_folder_id(pc, i));
				break;
			case SP_PLAYLIST_TYPE_PLACEHOLDER:
				printf("%d. Placeholder", i);
				break;
		}
	}
  cmd_logout(argc, argv);
  container_context_free(ctx);
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

