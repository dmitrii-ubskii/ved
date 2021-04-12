#ifndef SRC_OPS_H_
#define SRC_OPS_H_

#include <unordered_map>

#include "ncursespp/ncurses.h"
#include "ncursespp/keys.h"

#include "editor.h"

struct OperatorArgs
{
	ncurses::Key const key;

	ncurses::Ncurses& context;

	Editor::Buffer& buffer;
	Editor::Register& reg;

	CursorPosition const cursor;
	WindowInfo const windowInfo;
	Editor::Mode const currentMode;

	ncurses::Key pendingOperator{ncurses::Key::Null};
	std::optional<int> const count{};
};

struct OperatorResult
{
	bool cursorMoved{false};
	CursorPosition cursorPosition{0, 0};

	bool bufferChanged{false};

	bool modeChanged{false};
	Editor::Mode newMode{Editor::Mode::Normal};

	std::string message{""};

	ncurses::Key pendingOperator{ncurses::Key::Null};
	std::optional<int> count{std::nullopt};
};

using OperatorFunction = OperatorResult(*)(OperatorArgs args);

OperatorResult moveCursor(OperatorArgs args);
OperatorResult scrollBuffer(OperatorArgs args);
OperatorResult moveToStartOfLine(OperatorArgs args);
OperatorResult handleDigit(OperatorArgs args);
OperatorResult deleteChars(OperatorArgs args);
OperatorResult breakLine(OperatorArgs args);
OperatorResult deleteLines(OperatorArgs args);
OperatorResult yankLines(OperatorArgs args);
OperatorResult doPendingOperator(OperatorArgs args);
OperatorResult putLines(OperatorArgs args);
OperatorResult replaceChars(OperatorArgs args);
OperatorResult redraw(OperatorArgs);
OperatorResult startInsert(OperatorArgs args);
OperatorResult startCommand(OperatorArgs);
OperatorResult startNormal(OperatorArgs args);

static auto normalOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	{ncurses::Key{'0'}, handleDigit},
	{ncurses::Key{'1'}, handleDigit},
	{ncurses::Key{'2'}, handleDigit},
	{ncurses::Key{'3'}, handleDigit},
	{ncurses::Key{'4'}, handleDigit},
	{ncurses::Key{'5'}, handleDigit},
	{ncurses::Key{'6'}, handleDigit},
	{ncurses::Key{'7'}, handleDigit},
	{ncurses::Key{'8'}, handleDigit},
	{ncurses::Key{'9'}, handleDigit},
	{ncurses::Key{' '}, moveCursor},
	{ncurses::Key::Right, moveCursor},
	{ncurses::Key::Backspace, moveCursor},
	{ncurses::Key::Left, moveCursor},
	{ncurses::Key{'$'}, moveCursor},
	{ncurses::Key::End, moveCursor},
	{ncurses::Key{'g'}, scrollBuffer},
	{ncurses::Key{'h'}, scrollBuffer},
	{ncurses::Key{'l'}, scrollBuffer},
	{ncurses::Key{'b'}, scrollBuffer},
	{ncurses::Key::Down, scrollBuffer},
	{ncurses::Key::Up, scrollBuffer},
	{ncurses::Key::Enter, moveToStartOfLine},
	{ncurses::Key{'-'}, moveToStartOfLine},
	{ncurses::Key::Home, moveToStartOfLine},
	{ncurses::Key{'x'}, deleteChars},
	{ncurses::Key{'r'}, replaceChars},  // any character
	{ncurses::Key{'d'}, doPendingOperator},   // dd or dy (delete / cut)
	{ncurses::Key{'y'}, doPendingOperator},     // yy or yd (yank / cut)
	{ncurses::Key{'p'}, putLines},
	{ncurses::Key{'P'}, putLines},
	{ncurses::Key{'i'}, startInsert},
	{ncurses::Key{'a'}, startInsert},
	{ncurses::Key{'o'}, startInsert},
	// TODO not implemented: Command mode
	// {ncurses::Key{'/'}, startCommand},
	{ncurses::Key{':'}, startCommand},
	{ncurses::Key{';'}, startCommand},
	{ncurses::Key{'z'}, redraw},
};

static auto insertOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	{ncurses::Key::Right, moveCursor},
	{ncurses::Key::Left, moveCursor},
	{ncurses::Key::Down, scrollBuffer},
	{ncurses::Key::Up, scrollBuffer},
	{ncurses::Key::End, moveCursor},
	{ncurses::Key::Home, moveToStartOfLine},
	{ncurses::Key::Escape, startNormal},
	{ncurses::Key::Backspace, deleteChars},
	{ncurses::Key::Enter, breakLine},
};

static auto commandOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	// {ncurses::Key::Right, ...},
	// {ncurses::Key::Left, ...},
	// {ncurses::Key::Down, ...},
	// {ncurses::Key::Up, ...},
	// {ncurses::Key::End, ...},
	// {ncurses::Key::Home, ...},
	{ncurses::Key::Escape, startNormal},
	// {ncurses::Key::Backspace, ...},
	// {ncurses::Key::Enter, ...},
};

#endif // SRC_OPS_H_
