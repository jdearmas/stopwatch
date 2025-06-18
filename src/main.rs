use std::fs::OpenOptions;
use std::io::{self, Write};
use std::time::{Duration, Instant};

use chrono::Local;
use crossterm::cursor::{MoveTo};
use crossterm::event::{poll, read, Event, KeyCode};
use crossterm::style::Print;
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, Clear, ClearType};
use crossterm::ExecutableCommand;

const MAX_SPLITS: usize = 100;
const LOG_FILE: &str = "done.org";

struct Split {
    name: String,
    start: Instant,
    end: Option<Instant>,
    parent: Option<usize>,
    level: usize,
}

fn format_time(dur: Duration) -> String {
    let ms = dur.as_millis() % 1000;
    let secs = dur.as_secs();
    let s = secs % 60;
    let m = (secs / 60) % 60;
    let h = secs / 3600;
    format!("{:02}:{:02}:{:02}.{:03}", h, m, s, ms)
}

fn clear_screen<W: Write>(out: &mut W) -> io::Result<()> {
    out.execute(Clear(ClearType::All))?;
    out.execute(MoveTo(0, 0))?;
    Ok(())
}

fn draw_static<W: Write>(out: &mut W, main_goal: &Option<String>, splits: &[Split]) -> io::Result<()> {
    clear_screen(out)?;
    out.execute(Print("=== Enhanced Stopwatch ===\n"))?;
    out.execute(Print(format!("Goal  : {}\n", main_goal.as_deref().unwrap_or("(none)"))))?;
    out.execute(Print("Time  : 00:00:00.000\n"))?;
    out.execute(Print(format!("Subgoals ({}):\n", splits.len())))?;
    for (i, split) in splits.iter().enumerate() {
        let indent = split.level * 2;
        out.execute(MoveTo(indent as u16, 3 + i as u16))?;
        let start = format_time(split.start.elapsed());
        if let Some(end) = split.end {
            let end_rel = end.duration_since(split.start);
            let dur = end_rel;
            out.execute(Print(format!("{:2}) {} -> {} = {} {}\n",
                i+1,
                start,
                format_time(end_rel),
                format_time(dur),
                split.name)))?;
        } else {
            out.execute(Print(format!("{:2}) --:--:--.--- -> --:--:--.--- = --:--:--.--- {}\n", i+1, split.name)))?;
        }
    }
    out.execute(Print("\nControls: s/start-stop r/reset g/start-subgoal n/nested-subgoal h/stop u/up d/redraw t/save-log q/quit\n"))?;
    out.flush()?;
    Ok(())
}

fn draw_dynamic<W: Write>(out: &mut W, start_time: Instant, elapsed: Duration, active: Option<usize>, splits: &[Split]) -> io::Result<()> {
    let now = Instant::now();
    let total = elapsed + (now.duration_since(start_time));
    let cur_time = format_time(total);
    out.execute(MoveTo(0, 1))?;
    out.execute(Print(format!("Time  : {}   ", cur_time)))?;
    if let Some(idx) = active {
        let split = &splits[idx];
        let rel = total.checked_sub(split.start.elapsed()).unwrap_or_default();
        let row = 3 + idx as u16;
        let indent = split.level * 2;
        out.execute(MoveTo(indent as u16, row))?;
        out.execute(Print(format!("{:2}) --:--:--.--- -> --:--:--.--- = {} {}\n",
            idx+1,
            format_time(rel),
            split.name)))?;
    }
    out.flush()?;
    Ok(())
}

fn save_log(main_goal: &str, start_instant: Instant, splits: &[Split]) -> io::Result<()> {
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(LOG_FILE)?;
    let start_dt = Local::now() - (Instant::now() - start_instant);
    let end_dt = Local::now();

    let total = end_dt.signed_duration_since(start_dt);
    let tot_str = format_time(Duration::from_millis(total.num_milliseconds() as u64));

    writeln!(file, "* {}", main_goal)?;
    writeln!(file, "  :LOGBOOK:")?;
    writeln!(file, "  CLOCK: [{}]--[{}] => {}", start_dt.format("%Y-%m-%d %H:%M"), end_dt.format("%Y-%m-%d %H:%M"), tot_str)?;
    writeln!(file, "  :END:\n")?;

    for split in splits.iter() {
        if let Some(end) = split.end {
            let start_rel = split.start.elapsed();
            let end_rel = end.duration_since(split.start);
            let dur = end_rel;
            let stars = "*".repeat(split.level + 2);
            writeln!(file, "{} {}", stars, split.name)?;
            writeln!(file, "  :LOGBOOK:")?;
            writeln!(file, "  CLOCK: [{}]--[{}] => {}",
                format_time(start_rel),
                format_time(end_rel),
                format_time(dur))?;
            writeln!(file, "  :END:\n")?;
        }
    }
    Ok(())
}

fn main() -> crossterm::Result<()> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();

    let mut running = false;
    let mut start_time = Instant::now();
    let mut elapsed = Duration::ZERO;
    let mut splits: Vec<Split> = Vec::with_capacity(MAX_SPLITS);
    let mut active: Option<usize> = None;
    let mut main_goal: Option<String> = None;

    draw_static(&mut stdout, &main_goal, &splits)?;

    loop {
        if poll(Duration::from_millis(30))? {
            if let Event::Key(key) = read()? {
                match key.code {
                    KeyCode::Char('s') => {
                        if running {
                            let now = Instant::now();
                            elapsed += now.duration_since(start_time);
                            running = false;
                        } else {
                            print!("\nEnter main goal: ");
                            disable_raw_mode()?;
                            io::stdout().flush().unwrap();
                            let mut input = String::new();
                            io::stdin().read_line(&mut input).unwrap();
                            enable_raw_mode()?;
                            main_goal = Some(input.trim().to_string());
                            start_time = Instant::now();
                            elapsed = Duration::ZERO;
                            splits.clear();
                            active = None;
                            running = true;
                        }
                        draw_static(&mut stdout, &main_goal, &splits)?;
                    }
                    KeyCode::Char('r') => {
                        running = false;
                        elapsed = Duration::ZERO;
                        splits.clear();
                        main_goal = None;
                        active = None;
                        draw_static(&mut stdout, &main_goal, &splits)?;
                    }
                    KeyCode::Char('g') if running && splits.len() < MAX_SPLITS => {
                        print!("\nEnter subgoal name: ");
                        disable_raw_mode()?;
                        io::stdout().flush().unwrap();
                        let mut input = String::new();
                        io::stdin().read_line(&mut input).unwrap();
                        enable_raw_mode()?;
                        let name = input.trim().to_string();
                        let parent = active;
                        let level = parent.map_or(0, |idx| splits[idx].level + 1);
                        splits.push(Split { name, start: Instant::now(), end: None, parent, level });
                        active = Some(splits.len() - 1);
                        draw_static(&mut stdout, &main_goal, &splits)?;
                    }
                    KeyCode::Char('n') if running && active.is_some() && splits.len() < MAX_SPLITS => {
                        print!("\nEnter nested subgoal name: ");
                        disable_raw_mode()?;
                        io::stdout().flush().unwrap();
                        let mut input = String::new();
                        io::stdin().read_line(&mut input).unwrap();
                        enable_raw_mode()?;
                        let name = input.trim().to_string();
                        let parent = active;
                        let level = splits[parent.unwrap()].level + 1;
                        splits.push(Split { name, start: Instant::now(), end: None, parent, level });
                        active = Some(splits.len() - 1);
                        draw_static(&mut stdout, &main_goal, &splits)?;
                    }
                    KeyCode::Char('h') => {
                        // stop current subgoal if there is one
                        if let Some(idx) = active {
                            let now = Instant::now();
                            splits[idx].end = Some(now);
                            active = splits[idx].parent;
                            draw_static(&mut stdout, &main_goal, &splits)?;
                        }
                    }
                    KeyCode::Char('u') => {
                        if let Some(idx) = active {
                            active = splits[idx].parent;
                            draw_static(&mut stdout, &main_goal, &splits)?;
                        }
                    }
                    KeyCode::Char('d') => {
                        // manual redraw
                        draw_static(&mut stdout, &main_goal, &splits)?;
                    }
                    KeyCode::Char('t') if !running => {
                        if let Some(goal) = &main_goal {
                            let _ = save_log(goal, start_time, &splits);
                        }
                    }
                    KeyCode::Char('q') => break,
                    _ => {}
                }
            }
        }

        if running {
            let _ = draw_dynamic(&mut stdout, start_time, elapsed, active, &splits);
        }
    }

    disable_raw_mode()?;
    Ok(())
}
