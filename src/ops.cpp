#include "ops.h"

[[nodiscard]] OperatorResult moveCursor(OperatorArgs args)
{
	auto cursor = args.cursor;

	auto lastValidOffset = [&](int index)
	{
		auto lineLength = args.buffer.lineLength(index);
		if (lineLength == 0)
			return 0;
		return lineLength + (args.currentMode == Editor::Mode::Insert ? 0 : -1);
	};

	auto toMove = args.count.value_or(1);

	switch (args.key)
	{
		case ' ':
		case ncurses::Key::Right:
			while (toMove)
			{
				auto lastValidPosOnLine = lastValidOffset(cursor.line);
				if (cursor.col + toMove <= lastValidPosOnLine)
				{
					cursor.col += toMove;
					break;
				}
				else if (cursor.line < args.buffer.numLines() - 1)
				{
					toMove -= lastValidPosOnLine - cursor.col + 1;
					cursor.col = 0;
					cursor.line++;
				}
				else
				{
					cursor.col = lastValidPosOnLine;
					break;
				}
			}
			break;

		case ncurses::Key::Backspace:
		case ncurses::Key::Left:
			while (toMove)
			{
				if (cursor.col >= toMove)
				{
					cursor.col -= toMove;
					break;
				}
				else if (cursor.line > 0)
				{
					toMove -= cursor.col + 1;
					cursor.line--;
					cursor.col = std::max(0, lastValidOffset(cursor.line));
				}
				else
				{
					cursor.col = 0;
					break;
				}
			}
			
			break;

		case '$':
		case ncurses::Key::End:
			cursor.col = std::max(0, lastValidOffset(cursor.line));
			break;

		default:
			throw;
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}

[[nodiscard]] OperatorResult scrollBuffer(OperatorArgs args)
{
	auto cursor = args.cursor;
	switch (args.key)
	{
		case 'g':
			cursor.line = std::min(args.count.value_or(args.buffer.numLines()) - 1, args.buffer.numLines() - 1);
			break;

		case 'h':
			cursor.line = args.windowInfo.topLine;
			break;

		case 'l':
			// FIXME get actual lowest visible line
			cursor.line = std::min(args.windowInfo.topLine + 22, args.buffer.numLines() - 1);
			break;

		case 'b':
			cursor.line = 0;
			break;

		case ncurses::Key::Down:
			cursor.line = std::min(cursor.line + args.count.value_or(1), args.buffer.numLines() - 1);
			break;

		case ncurses::Key::Up:
			cursor.line = std::max(cursor.line - args.count.value_or(1), 0);
			break;

		default:
			throw;
	}
	int cursorLineLength = args.buffer.lineLength(cursor.line);
	if (cursor.col > cursorLineLength)
	{
		cursor.col = std::max(0, cursorLineLength - 1);
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}


[[nodiscard]] OperatorResult moveToStartOfLine(OperatorArgs args)
{
	auto cursor = args.cursor;
	cursor.col = 0;
	switch (args.key)
	{
		case ncurses::Key::Enter:
			cursor.line = std::min(cursor.line + args.count.value_or(1), args.buffer.numLines() - 1);
			break;

		case '-':
			cursor.line = std::max(cursor.line - args.count.value_or(1), 0);
			break;

		case '0':
		case ncurses::Key::Home:
			/* stay on this line */
			break;

		default:
			throw;
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}

[[nodiscard]] OperatorResult handleDigit(OperatorArgs args)
{
	switch (args.key)
	{
		case '0':
			if (not args.count.has_value())
			{
				return moveToStartOfLine(args);
			}
			[[fallthrough]];

		case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			return {
				.pendingOperator=args.pendingOperator,
				.count=args.count.value_or(0) * 10 + args.key.keycode - '0'
			};
			break;

		default:
			throw;
	}
}

[[nodiscard]] OperatorResult deleteChars(OperatorArgs args)
{
	auto result = OperatorResult{.bufferChanged=true};
	switch (args.key)
	{
		case 'x':
			if (args.buffer.lineLength(args.cursor.line) == 0)
			{
				result.bufferChanged = false;
			}
			else
			{
				args.buffer.erase(args.cursor, args.count.value_or(1));
				int cursorLineLength = args.buffer.lineLength(args.cursor.line);
				if (cursorLineLength == 0)
				{
					result.cursorMoved = true;
					result.cursorPosition = {.line=args.cursor.line, .col=0};
				}
				else if (args.cursor.col >= cursorLineLength)
				{
					result.cursorMoved = true;
					result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col - 1};
				}
			}
			break;

		case ncurses::Key::Backspace:
			if (args.cursor.col > 0)
			{
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col - 1};
				args.buffer.erase(result.cursorPosition, 1);
			}
			else if (args.cursor.line > 0)
			{
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line - 1, .col=args.buffer.lineLength(args.cursor.line - 1)};
				args.buffer.joinLines(result.cursorPosition.line, 2);
			}
			break;

		default:
			throw;
	}
	return result;
}

[[nodiscard]] OperatorResult breakLine(OperatorArgs args)
{
	args.buffer.breakLine(args.cursor);
	return {.cursorMoved=true, .cursorPosition={.line=args.cursor.line + 1, .col=0}, .bufferChanged=true};
}

[[nodiscard]] OperatorResult deleteLines(OperatorArgs args)
{
	OperatorResult result{.bufferChanged=true};

	auto count = args.count.value_or(1);
	switch (args.key)
	{
		case 'd':  // dd
			break;

		case 'y':  // dy
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			break;

		default:
			throw;
	}
	args.buffer.deleteLines(args.cursor.line, count);
	result.message = std::to_string(count) + " fewer lines";

	result.cursorPosition = args.cursor;
	if (result.cursorPosition.line >= args.buffer.numLines())
	{
		result.cursorMoved = true;
		result.cursorPosition.line = args.buffer.numLines() - 1;
	}
	if (result.cursorPosition.col >= args.buffer.lineLength(result.cursorPosition.line))
	{
		result.cursorMoved = true;
		result.cursorPosition.col = std::max(0, args.buffer.lineLength(result.cursorPosition.line) - 1);
	}

	return result;
}

[[nodiscard]] OperatorResult yankLines(OperatorArgs args)
{
	auto count = args.count.value_or(1);
	switch (args.key)
	{
		case 'd':  // yd
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			// NOTE deleteLines() assumes pendingOperator == 'd', so it's equivalent to 'dd'
			return deleteLines(args);

		case 'y':  // yy
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			break;

		default:
			throw;
	}
	return {.message=std::to_string(count) + " lines yanked"};
}

[[nodiscard]] OperatorResult doPendingOperator(OperatorArgs args)
{
	if (args.pendingOperator == ncurses::Key::Null)
	{
		return {.pendingOperator=args.key, .count=args.count};
	}

	switch (args.pendingOperator)
	{
		case 'd':
			return deleteLines(args);

		case 'y':
			return yankLines(args);

		default:
			throw;
	}
}

[[nodiscard]] OperatorResult putLines(OperatorArgs args)
{
	switch (args.key)
	{
		case 'p':
			args.buffer.putFrom(args.reg, args.cursor.line);
			return {.cursorMoved=true, .cursorPosition={args.cursor.line+1, 0}, .bufferChanged=true};

		case 'P':
			args.buffer.putFrom(args.reg, args.cursor.line-1);
			return {.cursorMoved=true, .cursorPosition={args.cursor.line, 0}, .bufferChanged=true};

		default:
			throw;
	}
}

[[nodiscard]] OperatorResult replaceChars(OperatorArgs args)
{
	if (args.key != 'r')
	{
		throw;
	}
	auto c = args.context.getch();
	auto count = std::min(args.count.value_or(1), args.buffer.lineLength(args.cursor.line) - args.cursor.col);
	args.buffer.erase(args.cursor, count);
	args.buffer.insert(args.cursor, c, count);
	return {.bufferChanged=true};
}

[[nodiscard]] OperatorResult redraw(OperatorArgs)
{
	return {.bufferChanged=true};  // LIES!
}

[[nodiscard]] OperatorResult startInsert(OperatorArgs args)
{
	OperatorResult result{.modeChanged=true, .newMode=Editor::Mode::Insert};

	switch (args.key)
	{
		case 'i':
			break;

		case 'a':
			if (args.buffer.lineLength(args.cursor.line) > 0)
			{
				// if the line is not empty, we're guaranteed that the column after the cursor is a valid spot
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col + 1};
			}
			break;

		case 'o':
			args.buffer.insertLine(args.cursor.line);
			result.bufferChanged = true;
			result.cursorMoved = true;
			result.cursorPosition = {.line=args.cursor.line + 1, .col=0};
			break;

		default:
			throw;
	}

	return result;
}

[[nodiscard]] OperatorResult startCommand(OperatorArgs)
{
	return {
		.modeChanged=true, .newMode=Editor::Mode::Command
	};
}

[[nodiscard]] OperatorResult startNormal(OperatorArgs args)
{
	return {
		.cursorMoved=true, .cursorPosition={args.cursor.line, std::max(args.cursor.col-1, 0)},
		.modeChanged=true, .newMode=Editor::Mode::Normal
	};
}

// *** //

CommandOperatorResult startNormal(CommandOperatorArgs)
{
	return {.modeChanged=true, .newMode=Editor::Mode::Normal};
}

CommandOperatorResult deleteCmdlineChars(CommandOperatorArgs args)
{
	switch (args.key)
	{
		case ncurses::Key::Backspace:
			if (args.cmdlineCursor > 0)
			{
				args.cmdline.erase(args.cmdlineCursor - 1);
				return {.cursorMoved=true, .cursorPosition=args.cmdlineCursor-1, .cmdlineChanged=true};
			}
			else
			{
				return {};
			}
			break;

		default:
			throw;
	}
}

CommandOperatorResult parseCmdline(CommandOperatorArgs args)
{
	auto result = CommandOperatorResult{.modeChanged=true, .newMode=Editor::Mode::Normal};

	if (args.cmdline == "")
	{
		return result;
	}

	auto firstSpace = args.cmdline.find(" ");
	auto command = args.cmdline.substr(0, firstSpace);
	if (command.ends_with("!"))
	{
		result.parsedCommand.push_back(command.substr(0, command.length() - 1));
		result.parsedCommand.push_back("!");
	}
	else
	{
		result.parsedCommand.push_back(command);
	}

	if (firstSpace != std::string::npos)
	{
		auto tailStart = args.cmdline.find_first_not_of(" ", firstSpace);
		if (tailStart != std::string::npos)
		{
			auto tail = args.cmdline.substr(tailStart, args.cmdline.find_last_not_of(" "));
			if (tail.find_first_of(" ") != std::string::npos)
			{
				result.message = "ERR: Trailing characters";
				result.parsedCommand = {};
				return result;
			}
			result.parsedCommand.push_back(tail);
		}
	}

	return result;
}

