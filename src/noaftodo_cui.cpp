#include "noaftodo_cui.h"

#include <codecvt>
#ifdef __sun
#include <ncurses/curses.h>
# else
#include <curses.h>
#endif
#include <locale>

#include "noaftodo.h"
#include "noaftodo_cmd.h"
#include "noaftodo_config.h"
#include "noaftodo_output.h"
#include "noaftodo_time.h"

using namespace std;

map<char, cui_col_s> cui_columns;

int cui_mode;
stack<int> cui_prev_modes;

vector<cui_bind_s> binds;

int cui_w, cui_h;

string cui_status = "Welcome to " + string(TITLE) + "!";

int cui_s_line;
int cui_delta;
int cui_numbuffer = -1;

vector<wstring> cui_commands;
int cui_command_cursor = 0;
int cui_command_index = 0;

wstring_convert<codecvt_utf8<wchar_t>, wchar_t> w_converter;

extern char _binary_doc_doc_gen_start;
extern char _binary_doc_doc_gen_end;

void cui_init()
{
	log("Initializing console UI...");

	// initialize columns
	cui_columns['t'] = 
	{ 
		"Task Title", 
		[](const int& w, const int& free, const int& cols)
		{
			return free / 4;
		},
		[](const noaftodo_entry& e, const int& id) 
		{ 
			return e.title; 
		} 
	};
	cui_columns['l'] = 
	{ 
		"List", 
		[](const int& w, const int& free, const int& cols)
		{
			return free / 10;
		},
		[](const noaftodo_entry& e, const int& id) 
		{ 
			if (e.tag < t_tags.size())
			       if (t_tags.at(e.tag) != to_string(e.tag))
				       return to_string(e.tag) + ": " + t_tags.at(e.tag);

			return "List " + t_tags.at(e.tag);
		} 
	};
	cui_columns['d'] = 
	{ 
		"Due", 
		[](const int& w, const int& free, const int& cols)
		{
			return 16;
		},
		[](const noaftodo_entry& e, const int& id) 
		{ 
			return ti_f_str(e.due); 
		} 
	};
	cui_columns['D'] = 
	{ 
		"Task description", 
		[](const int& w, const int& free, const int& cols)
		{
			return free;
		},
		[](const noaftodo_entry& e, const int& id) 
		{ 
			return e.description; 
		} 
	};
	cui_columns['i'] = 
	{ 
		"ID", 
		[](const int& w, const int& free, const int& cols)
		{
			return 3;
		},
		[](const noaftodo_entry& e, const int& id) 
		{ 
			return to_string(id); 
		} 
	};
	
	// construct UI
	cui_commands.push_back(w_converter.from_bytes(""));

	initscr();
	start_color();
	use_default_colors();
	init_pair(CUI_CP_TITLE, conf_get_cvar_int("colors.title"), conf_get_cvar_int("colors.background"));
	init_pair(CUI_CP_GREEN_ENTRY, conf_get_cvar_int("colors.entry_completed"), conf_get_cvar_int("colors.background"));
	init_pair(CUI_CP_YELLOW_ENTRY, conf_get_cvar_int("colors.entry_coming"), conf_get_cvar_int("colors.background"));
	init_pair(CUI_CP_RED_ENTRY, conf_get_cvar_int("colors.entry_failed"), conf_get_cvar_int("colors.background"));

	cbreak();
	set_escdelay(0);
	curs_set(0);
	noecho();
	keypad(stdscr, true);

	cui_w = getmaxx(stdscr);
	cui_h = getmaxy(stdscr);
}

void cui_destroy()
{
	endwin();
}

void cui_run()
{
	cui_init();
	cui_set_mode(CUI_MODE_NORMAL);

	for (wint_t c = 0; ; get_wch(&c))
	{
		bool bind_fired = false;
		for (const auto& bind : binds)
			if ((bind.mode & cui_mode) && (bind.key == c))
			{
				if (bind.autoexec) cmd_exec(bind.command);
				else 
				{
					cui_commands[cui_commands.size() - 1] = w_converter.from_bytes(bind.command);
					cui_set_mode(CUI_MODE_COMMAND);
				}

				bind_fired = true;
			}

		if (bind_fired) cui_numbuffer = -1;

		if (!bind_fired) switch (cui_mode)
		{
			case CUI_MODE_NORMAL:
				cui_normal_input(c);
				break;
			case CUI_MODE_DETAILS:
				cui_details_input(c);
				break;
			case CUI_MODE_COMMAND:
				cui_command_input(c);
				break;
			case CUI_MODE_HELP:
				cui_help_input(c);
		}

		cui_w = getmaxx(stdscr);
		cui_h = getmaxy(stdscr);

		clear();

		switch (cui_mode)
		{
			case CUI_MODE_NORMAL:
				cui_normal_paint();
				break;
			case CUI_MODE_DETAILS:
				cui_details_paint();
				break;
			case CUI_MODE_COMMAND:
				cui_command_paint();
				break;
			case CUI_MODE_HELP:
				cui_help_paint();
				break;
		}

		if (cui_mode == CUI_MODE_EXIT) break;
	}

	li_save();

	cui_destroy();
}

void cui_set_mode(const int& mode)
{
	if (mode == -1)
	{
		cui_mode = cui_prev_modes.top();
		cui_prev_modes.pop();
	} else {
		if ((cui_mode != CUI_MODE_COMMAND) && (cui_mode != mode)) cui_prev_modes.push(cui_mode);
		cui_mode = mode;
	}

	cui_delta = 0;

	switch (mode)
	{
		case CUI_MODE_NORMAL:
			curs_set(0);
			break;
		case CUI_MODE_DETAILS:
			cui_delta = 0;
			curs_set(0);
			break;
		case CUI_MODE_COMMAND:
			curs_set(1);
			cui_command_index = cui_commands.size() - 1;
			cui_command_cursor = cui_commands[cui_commands.size() - 1].length();
			break;
		case CUI_MODE_HELP:
			cui_delta = 0;
			curs_set(0);
			break;
	}
}

void cui_bind(const cui_bind_s& bind)
{
	binds.push_back(bind);
}

void cui_bind(const wchar_t& key, const string& command, const int& mode, const bool& autoexec)
{
	cui_bind({ key, command, mode, autoexec });
}

bool cui_is_visible(const int& entryID)
{
	if (t_list.size() == 0) return false;
	const auto& entry = t_list.at(entryID);

	const int tag_filter = conf_get_cvar_int("tag_filter");
	const int filter = conf_get_cvar_int("filter");
	const bool tag_check = ((tag_filter == CUI_TAG_ALL) || (tag_filter == entry.tag));

	if (entry.completed) return tag_check && (filter & CUI_FILTER_COMPLETE);
	if (entry.due <= ti_to_long("a0d")) return tag_check && (filter & CUI_FILTER_FAILED);
	if (entry.due <= ti_to_long("a1d")) return tag_check && (filter & CUI_FILTER_COMING);

	return tag_check && (filter & CUI_FILTER_UNCAT);
}

void cui_normal_paint()
{
	const int tag_filter = conf_get_cvar_int("tag_filter");
	const int filter = conf_get_cvar_int("filter");

	// draw table title
	move(0, 0);
	attrset(A_STANDOUT | A_BOLD | COLOR_PAIR(CUI_CP_TITLE));
	for (int i = 0; i < cui_w; i++) addch(' ');

	int x = 0;
	const string cols = (tag_filter == CUI_TAG_ALL) ? conf_get_cvar("all_cols") : conf_get_cvar("cols");
	for (int coln = 0; coln < cols.length(); coln++)
	{
		try
		{
			const char& col = cols.at(coln);
			if (x >= cui_w) break;
			move(0, x);
			const int w = cui_columns.at(col).width(cui_w, cui_w - x, cols.length());
			addstr(cui_columns.at(col).title.c_str());

			if (coln < cols.length() - 1) if (x + w < cui_w)
			{
				move(0, x + w);
				addstr((" " + conf_get_cvar("charset.row_separator") + " ").c_str());
			}
			x += w + 3;
		} catch (out_of_range e) {}
	}
	attrset(A_NORMAL);

	vector<int> v_list;
	int cui_v_line = -1;
	for (int l = 0; l < t_list.size(); l++)
		if (cui_is_visible(l)) 
		{
			v_list.push_back(l);
			if (l == cui_s_line) cui_v_line = v_list.size() - 1;
		}

	if (v_list.size() != 0) 
	{
		while (!cui_is_visible(cui_s_line)) cmd_exec("down");
		for (int i = 0; i < v_list.size(); i++) if (v_list.at(i) == cui_s_line) cui_v_line = i;
	}

	cui_delta = 0;
	if (cui_v_line - cui_delta >= cui_h - 2) cui_delta = cui_v_line - cui_h + 3;
	if (cui_v_line - cui_delta < 0) cui_delta = cui_v_line;

	int last_string = 0;
	if (v_list.size() > 0) 
	{
		for (int l = 0; l < v_list.size(); l++)
		{
			if (l - cui_delta >= cui_h - 2) break;
			if (l >= cui_delta)    
			{
				const noaftodo_entry& entry = t_list.at(v_list.at(l));
				
				if (l == cui_v_line) attron(A_STANDOUT);
				if (entry.completed) attron(COLOR_PAIR(CUI_CP_GREEN_ENTRY) | A_BOLD);	// a completed entry
				else if (entry.due <= ti_to_long("a0d")) attron(COLOR_PAIR(CUI_CP_RED_ENTRY) | A_BOLD);	// a failed entry
				else if (entry.due <= ti_to_long("a1d")) attron(COLOR_PAIR(CUI_CP_YELLOW_ENTRY) | A_BOLD);	// an upcoming entry

				x = 0;
				move(l - cui_delta + 1, x);
				for (int i = 0; i < cui_w; i++) addch(' ');

				for (int coln = 0; coln < cols.length(); coln++)
				{
					try
					{
						const char& col = cols.at(coln);
						if (x >= cui_w) break;
						move(l - cui_delta + 1, x);
						const int w = cui_columns.at(col).width(cui_w, cui_w - x, cols.length());
						addstr((cui_columns.at(col).contents(entry, v_list.at(l))).c_str());

						if (coln < cols.length() - 1) if (x + w < cui_w)
						{
							move(l - cui_delta + 1, x + w);
							addstr((" " + conf_get_cvar("charset.row_separator") + " ").c_str());
						}
						x += w + 3;
					} catch (out_of_range e) {}
				}

				move(l - cui_delta + 1, cui_w - 1);
				addstr(" ");

				attrset(A_NORMAL);
				last_string = l - cui_delta + 2;
			}
		}

		for (int s = last_string; s < cui_h; s++) { move(s, 0); clrtoeol(); }
	}

	cui_status = 	((tag_filter == CUI_TAG_ALL) ?
				"All lists" :
				("List " + to_string(tag_filter) + (((tag_filter < t_tags.size()) && (t_tags.at(tag_filter) != to_string(tag_filter))) ? (": " + t_tags.at(tag_filter)) : ""))) +
			" " + conf_get_cvar("charset.status_separator") + " " +
			string((filter & CUI_FILTER_UNCAT) ? "U" : "_") +
			string((filter & CUI_FILTER_COMPLETE) ? "V" : "_") +
			string((filter & CUI_FILTER_COMING) ? "C" : "_") +
			string((filter & CUI_FILTER_FAILED) ? "F" : "_") +
			((t_list.size() == 0) ? "" : " " + conf_get_cvar("charset.status_separator") + " " + to_string(cui_s_line) + "/" + to_string(t_list.size() - 1)) +
			string((cui_status != "") ? (" " + conf_get_cvar("charset.status_separator") + " " + cui_status) : "");

	move(cui_h - 1, cui_w - 1 - cui_status.length());
	addstr(cui_status.c_str());
	cui_status = "";
}

void cui_normal_input(const wchar_t& key)
{
	switch (key)
	{
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (cui_numbuffer == -1) cui_numbuffer = 0;
			cui_numbuffer = cui_numbuffer * 10 + (key - '0');
			cui_status = to_string(cui_numbuffer);
			break;
		case 'g':
			if (cui_numbuffer == -1) 
			{
				cui_numbuffer = 0;
				cui_status = 'g';
			} else {
				cmd_exec("g " + to_string(cui_numbuffer));
				cui_numbuffer = -1;
			}
			break;
		case 'l':
			if (cui_numbuffer == -1)
			{
				cui_numbuffer = -2;
				cui_status = 'l';
			} else {
				if (cui_numbuffer == -2) cmd_exec("list all");
				else cmd_exec("list " + to_string(cui_numbuffer));
				cui_numbuffer = -1;
			}
			break;
		case 'G':
			cui_numbuffer = t_list.size() - 1;
			cui_status = 'G';
			break;
		default:
			cui_numbuffer = -1;
			break;
	}
}

void cui_details_paint()
{
	const int tdelta = cui_delta;
	cui_normal_paint();

	// draw details box
	move(2, 3);
	addstr(conf_get_cvar("charset.box_corner_1").c_str());
	move(cui_h - 3, 3);
	addstr(conf_get_cvar("charset.box_corner_3").c_str());
	move(2, cui_w - 4);
	addstr(conf_get_cvar("charset.box_corner_2").c_str());
	move(cui_h - 3, cui_w - 4);
	addstr(conf_get_cvar("charset.box_corner_4").c_str());

	for (int i = 3; i <= cui_h - 4; i++) 
	{ 
		move(i, 3); 
		addstr(conf_get_cvar("charset.box_border_v").c_str()); 

		move(i, cui_w - 4);
		addstr(conf_get_cvar("charset.box_border_v").c_str()); 
	}

	for (int j = 4; j < cui_w - 4; j++)
	{
		move(2, j);
		addstr(conf_get_cvar("charset.box_border_h").c_str());

		for (int i = 3; i < cui_h - 3; i++)
		{
			move(i, j);
			addch(' ');
		}

		move(cui_h - 3, j);
		addstr(conf_get_cvar("charset.box_border_h").c_str());
	}

	// fill the box with details
	// Title
	const noaftodo_entry& entry = t_list.at(cui_s_line);
	move(4, 5);
	addstr(entry.title.c_str());

	for (int i = 4; i < cui_w - 4; i++)
	{
		move(6, i);
		addstr(conf_get_cvar("charset.box_ui_line_h").c_str());
	}

	move(7, 5);
	
	string tag = "";
	if (entry.tag < t_tags.size()) if (t_tags.at(entry.tag) != to_string(entry.tag))
		tag = ": " + t_tags.at(entry.tag);

	addstr((ti_f_str(entry.due) +
		" " + conf_get_cvar("charset.status_separator") + " " + 
		"List " + to_string(entry.tag) + 
		tag).c_str()); 

	for (int i = 4; i < cui_w - 4; i++)
	{
		move(8, i);
		addstr(conf_get_cvar("charset.box_ui_line_h").c_str());
	}

	// draw description
	// we want text wrapping here
	wstring desc = w_converter.from_bytes(entry.description);
	int x = 5;
	int y = 10 + tdelta;
	for (int i = 0; i < desc.length(); i++)
	{
		if (x == cui_w - 5)
		{
			x = 5;
			y++;
		}

		if (y == cui_h - 4) 
		{
			move(cui_h - 4, 5);
			addstr("<- ... ->");
			break; // could've implemented scrolling
				// but come on, who writes descriptions
				// that long in a TODO-list :)
		}

		move(y, x);
		if (y >= 10) addstr(w_converter.to_bytes(desc.at(i)).c_str());

		x++;

		cui_delta = tdelta;
	}
}

void cui_details_input(const wchar_t& key)
{
	switch (key)
	{
		case 'q': case 27:
			cui_set_mode(-1);
			break;
		case KEY_RIGHT:
			cui_delta--;
			break;
		case KEY_LEFT:
			cui_delta++;
			break;
		case '=':
			cui_delta = 0;
			break;
		default:
			break;
	}
}

void cui_command_paint()
{
	switch (cui_prev_modes.top())
	{
		case CUI_MODE_NORMAL:
			cui_normal_paint();
			break;
		case CUI_MODE_DETAILS:
			cui_details_paint();
			break;
		case CUI_MODE_HELP:
			cui_help_paint();
			break;
	}

	move(cui_h - 1, 0);
	clrtoeol();
	move(cui_h - 1, 0);
	int offset = cui_command_cursor - cui_w + 3;
	if (offset < 0) offset = 0;
	addstr(w_converter.to_bytes(L":" + cui_commands[cui_commands.size() - 1].substr(offset)).c_str());
	move(cui_h - 1, 1 + cui_command_cursor - offset);
}

void cui_command_input(const wchar_t& key)
{
	switch (key)
	{
		case 10:
			if (cui_commands[cui_commands.size() - 1] != w_converter.from_bytes(""))
			{
				cmd_exec(w_converter.to_bytes(cui_commands[cui_commands.size() - 1]));
				if (cui_command_index != cui_commands.size() - 1)
				{
					wstring temp = cui_commands[cui_commands.size() - 1];
					cui_commands[cui_command_index] = cui_commands[cui_commands.size() - 1];
					cui_commands[cui_commands.size() - 1] = temp;
				}

				cui_commands.push_back(w_converter.from_bytes(""));
				cui_command_index = cui_commands.size() - 1;
			}
		case 27:
			cui_commands[cui_commands.size() - 1] = L"";
			if (cui_mode == CUI_MODE_COMMAND) cui_set_mode(CUI_MODE_NORMAL);
			cui_filter_history();
			break;
		case 127: case KEY_BACKSPACE: // 127 is for, e.g., xfce4-terminal
						// KEY_BACKSPACE - e.g., alacritty
			if (cui_command_index != cui_commands.size() - 1)
			{
				wstring temp = cui_commands[cui_commands.size() - 1];
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_command_index];
				cui_commands[cui_command_index] = temp;
				cui_commands.push_back(temp);
				cui_command_index = cui_commands.size() - 1;
			}

			if (cui_command_cursor > 0)
			{
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_commands.size() - 1].substr(0, cui_command_cursor - 1) + cui_commands[cui_commands.size() - 1].substr(cui_command_cursor, cui_commands[cui_commands.size() - 1].length() - cui_command_cursor);
				cui_command_cursor--;
			}
			break;
		case KEY_DC:
			if (cui_command_index != cui_commands.size() - 1)
			{
				wstring temp = cui_commands[cui_commands.size() - 1];
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_command_index];
				cui_commands[cui_command_index] = temp;
				cui_commands.push_back(temp);
				cui_command_index = cui_commands.size() - 1;
			}

			if (cui_command_cursor < cui_commands[cui_commands.size() - 1].length())
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_commands.size() - 1].substr(0, cui_command_cursor) + cui_commands[cui_commands.size() - 1].substr(cui_command_cursor + 1, cui_commands[cui_commands.size() - 1].length() - cui_command_cursor - 1);
			break;
		case KEY_LEFT:
			if (cui_command_cursor > 0) cui_command_cursor--;
			break;
		case KEY_RIGHT:
			if (cui_command_cursor < cui_commands[cui_commands.size() - 1].length()) cui_command_cursor++;
			break;
		case KEY_UP:
			// go up the history
			if (cui_command_index > 0)
			{
				wstring temp = cui_commands[cui_command_index];
				cui_commands[cui_command_index] = cui_commands[cui_commands.size() - 1];
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_command_index - 1];
				cui_commands[cui_command_index - 1] = temp;
				cui_command_index--;
				if (cui_command_cursor > cui_commands[cui_commands.size() - 1].length())
					cui_command_cursor = cui_commands[cui_commands.size() - 1].length();
			}
			break;
		case KEY_DOWN:
			// go down the history
			if (cui_command_index < cui_commands.size() - 1)
			{
				wstring temp = cui_commands[cui_command_index];
				cui_commands[cui_command_index] = cui_commands[cui_commands.size() - 1];
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_command_index + 1];
				cui_commands[cui_command_index + 1] = temp;
				cui_command_index++;
				if (cui_command_cursor > cui_commands[cui_commands.size() - 1].length())
					cui_command_cursor = cui_commands[cui_commands.size() - 1].length();
			}	
			break;
		case KEY_HOME:
			cui_command_cursor = 0;
			break;
		case KEY_END:
			cui_command_cursor = cui_commands[cui_commands.size() - 1].length();
			break;
		default:
			if (cui_command_index != cui_commands.size() - 1)
			{
				wstring temp = cui_commands[cui_commands.size() - 1];
				cui_commands[cui_commands.size() - 1] = cui_commands[cui_command_index];
				cui_commands[cui_command_index] = temp;
				cui_commands.push_back(temp);
				cui_command_index = cui_commands.size() - 1;
			}

			cui_filter_history();

			cui_commands[cui_commands.size() - 1] = cui_commands[cui_commands.size() - 1].substr(0, cui_command_cursor) + key + cui_commands[cui_commands.size() - 1].substr(cui_command_cursor, cui_commands[cui_commands.size() - 1].length() - cui_command_cursor);
			cui_command_cursor++;
	}
}

void cui_help_paint()
{
	const int tdelta = cui_delta;
	cui_normal_paint();

	// draw help box
	move(2, 3);
	addstr(conf_get_cvar("charset.box_corner_1").c_str());
	move(cui_h - 3, 3);
	addstr(conf_get_cvar("charset.box_corner_3").c_str());
	move(2, cui_w - 4);
	addstr(conf_get_cvar("charset.box_corner_2").c_str());
	move(cui_h - 3, cui_w - 4);
	addstr(conf_get_cvar("charset.box_corner_4").c_str());

	for (int i = 3; i <= cui_h - 4; i++) 
	{ 
		move(i, 3); 
		addstr(conf_get_cvar("charset.box_border_v").c_str()); 

		move(i, cui_w - 4);
		addstr(conf_get_cvar("charset.box_border_v").c_str()); 
	}

	for (int j = 4; j < cui_w - 4; j++)
	{
		move(2, j);
		addstr(conf_get_cvar("charset.box_border_h").c_str());

		for (int i = 3; i < cui_h - 3; i++)
		{
			move(i, j);
			addch(' ');
		}

		move(cui_h - 3, j);
		addstr(conf_get_cvar("charset.box_border_h").c_str());
	}

	// fill the box
	move(4, 5);
	addstr((string(TITLE) + " v." + VERSION).c_str());

	for (int i = 4; i < cui_w - 4; i++)
	{
		move(6, i);
		addstr(conf_get_cvar("charset.box_ui_line_h").c_str());
	}

	// draw description
	// we want text wrapping here
	int x = 5;
	int y = 8 + tdelta;
	string cui_help;
	for (char* c = &_binary_doc_doc_gen_start; c < &_binary_doc_doc_gen_end; c++)
		cui_help += string(1, *c);

	for (int i = 0; i < cui_help.length(); i++)
	{
		if (x == cui_w - 5)
		{
			x = 5;
			y++;
		}
		
		if (y >= cui_h - 4) 
		{
			move(cui_h - 4, 5);
			addstr("<- ... ->");
			break;
		}

		const char c = cui_help.at(i);

		move(y, x);

		constexpr int TAB_W = 20;
		switch (c)
		{
			case '\n':
				y++;
				x = 5;
				break;
			case '\t':
				x = TAB_W;
				break;
			default:
				if (y >= 8) addch(c);
				x++;
		}
	}

	cui_delta = tdelta;
}

void cui_help_input(const wchar_t& key)
{
	switch (key)
	{
		case 'q': case 27:
			cui_set_mode(-1);
			break;
		case KEY_RIGHT:
			cui_delta--;
			break;
		case KEY_LEFT:
			cui_delta++;
			break;
		case '=':
			cui_delta = 0;
			break;
	}
}

void cui_filter_history()
{
	for (int i = 0; i < cui_commands.size() - 1; i++)
		if (cui_commands[i] == w_converter.from_bytes("")) 
		{
			cui_commands.erase(cui_commands.begin() + i);
			i--;
		}
}
