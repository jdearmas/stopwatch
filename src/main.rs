use std::{
    env,
    fs::OpenOptions,
    io::{self, Write},
    sync::mpsc,
    thread,
    time::{Duration, Instant},
};

use chrono::{DateTime, Local};
use crossterm::cursor::MoveTo;
use crossterm::event::{poll, read, Event, KeyCode};
use crossterm::style::Print;
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, Clear, ClearType};
use crossterm::ExecutableCommand;

const MAX_SPLITS: usize = 100;
const TICK_RATE_MS: u64 = 30;

struct Split {
    name: String,
    start_offset: Duration,
    end_offset: Option<Duration>,
    start_dt: DateTime<Local>,
    end_dt: Option<DateTime<Local>>,
    parent: Option<usize>,
    level: usize,
}

enum Message {
    Tick,
    Input(Event),
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

fn draw_static<W: Write>(
    out: &mut W,
    main_goal: &Option<String>,
    total: Duration,
    splits: &[Split],
) -> io::Result<()> {
    clear_screen(out)?;
    out.execute(Print("=== Stopwatch ==="))?;
    out.execute(MoveTo(0, 1))?;
    out.execute(Print(format!(
        "Goal  : {}",
        main_goal.as_deref().unwrap_or("(none)")
    )))?;
    out.execute(MoveTo(0, 2))?;
    out.execute(Print(format!("Time  : {}", format_time(total))))?;
    out.execute(MoveTo(0, 3))?;
    out.execute(Print(format!("Subgoals ({}):", splits.len())))?;
    out.execute(MoveTo(0, 4))?;
    for (i, split) in splits.iter().enumerate() {
        let indent = (split.level * 2) as u16;
        out.execute(MoveTo(indent, 4 + i as u16))?;
        let start_str = format_time(split.start_offset);
        if let Some(end_off) = split.end_offset {
            let dur = end_off.checked_sub(split.start_offset).unwrap_or_default();
            out.execute(Print(format!(
                "{:2}) {} -> {} = {} {}",
                i + 1,
                start_str,
                format_time(end_off),
                format_time(dur),
                split.name
            )))?;
        } else {
            out.execute(Print(format!(
                "{:2}) {} -> --:--:--.--- = --:--:--.--- {}",
                i + 1,
                start_str,
                split.name
            )))?;
        }
    }

    let controls_line_row = 4 + splits.len() as u16 + 1;
    out.execute(MoveTo(0, controls_line_row));
    out.execute(Print("\nControls: s=start/stop r=reset c=continue g=subgoal n=nested h=stop u=up d=redraw t=save-log q=quit\n"))?;
    out.flush()?;
    Ok(())
}

fn draw_dynamic<W: Write>(
    out: &mut W,
    start_time: Instant,
    elapsed: Duration,
    splits: &[Split],
    main_goal: &Option<String>,
) -> io::Result<()> {
    let now = Instant::now();
    let total = elapsed + now.duration_since(start_time);

    // redraw goal and time
    out.execute(MoveTo(0, 1))?;
    out.execute(Print(format!(
        "Goal  : {}   ",
        main_goal.as_deref().unwrap_or("(none)")
    )))?;
    let cur_time = format_time(total);
    out.execute(MoveTo(0, 2))?;
    out.execute(Print(format!("Time  : {}   ", cur_time)))?;

    // redraw running subgoals
    for (i, split) in splits.iter().enumerate() {
        if split.end_offset.is_none() {
            let rel = total.checked_sub(split.start_offset).unwrap_or_default();
            let row = 4 + i as u16;
            let indent = (split.level * 2) as u16;
            let start_str = format_time(split.start_offset);
            out.execute(MoveTo(indent, row))?;
            out.execute(Print(format!(
                "{:2}) {} -> {} = {} {}\n",
                i + 1,
                start_str,
                format_time(total),
                format_time(rel),
                split.name
            )))?;
        }
    }
    out.flush()?;
    Ok(())
}

fn save_log(
    main_goal: &str,
    start_instant: Instant,
    splits: &[Split],
    log_file: &str,
) -> io::Result<()> {
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(log_file)?;
    let start_dt = Local::now() - (Instant::now() - start_instant);
    let end_dt = Local::now();
    let total = end_dt.signed_duration_since(start_dt);
    let tot_str = format_time(Duration::from_millis(total.num_milliseconds() as u64));

    writeln!(file, "* {}", main_goal)?;
    writeln!(file, "  :LOGBOOK:")?;
    writeln!(
        file,
        "  CLOCK: [{}]--[{}] => {}",
        start_dt.format("%Y-%m-%d %H:%M"),
        end_dt.format("%Y-%m-%d %H:%M"),
        tot_str
    )?;
    writeln!(file, "  :END:\n")?;

    for split in splits {
        if let (Some(end_dt), Some(end_off)) = (split.end_dt, split.end_offset) {
            let dur = end_off.checked_sub(split.start_offset).unwrap_or_default();
            let stars = "*".repeat(split.level + 2);
            writeln!(file, "{} {}", stars, split.name)?;
            writeln!(file, "  :LOGBOOK:")?;
            writeln!(
                file,
                "  CLOCK: [{}]--[{}] => {}",
                split.start_dt.format("%Y-%m-%d %H:%M"),
                end_dt.format("%Y-%m-%d %H:%M"),
                format_time(dur)
            )?;
            writeln!(file, "  :END:\n")?;
        }
    }
    Ok(())
}

fn main() -> crossterm::Result<()> {
    let log_file = env::args().nth(1).unwrap_or_else(|| "done.org".to_string());
    enable_raw_mode()?;
    let mut stdout = io::stdout();

    let (tx, rx) = mpsc::channel::<Message>();
    // ticker thread
    {
        let tx = tx.clone();
        thread::spawn(move || loop {
            thread::sleep(Duration::from_millis(TICK_RATE_MS));
            if tx.send(Message::Tick).is_err() {
                break;
            }
        });
    }
    // input thread
    {
        let tx = tx.clone();
        thread::spawn(move || loop {
            if poll(Duration::from_millis(TICK_RATE_MS)).unwrap_or(false) {
                if let Ok(evt) = read() {
                    if tx.send(Message::Input(evt)).is_err() {
                        break;
                    }
                }
            }
        });
    }

    let mut running = false;
    let mut start_time = Instant::now();
    let mut elapsed = Duration::ZERO;
    let mut splits: Vec<Split> = Vec::with_capacity(MAX_SPLITS);
    let mut active: Option<usize> = None;
    let mut main_goal: Option<String> = None;

    draw_static(&mut stdout, &main_goal, elapsed, &splits)?;

    for msg in rx {
        match msg {
            Message::Tick => {
                if running {
                    let _ = draw_dynamic(&mut stdout, start_time, elapsed, &splits, &main_goal);
                }
            }
            Message::Input(evt) => match evt {
                Event::Key(key) => match key.code {
                    KeyCode::Char('s') => {
                        if running {
                            elapsed += Instant::now().duration_since(start_time);
                            running = false;
                        } else {
                            disable_raw_mode()?;
                            print!("\nEnter main goal: ");
                            io::stdout().flush()?;
                            let mut input = String::new();
                            io::stdin().read_line(&mut input)?;
                            enable_raw_mode()?;
                            main_goal = Some(input.trim().to_string());
                            start_time = Instant::now();
                            elapsed = Duration::ZERO;
                            splits.clear();
                            active = None;
                            running = true;
                        }
                        draw_static(&mut stdout, &main_goal, elapsed, &splits)?;
                    }
                    KeyCode::Char('c') => {
                        if !running {
                            // continue from stopped
                            start_time = Instant::now();
                            running = true;
                        }
                    }
                    KeyCode::Char('r') => {
                        running = false;
                        elapsed = Duration::ZERO;
                        splits.clear();
                        main_goal = None;
                        active = None;
                        draw_static(&mut stdout, &main_goal, elapsed, &splits)?;
                    }
                    KeyCode::Char('g') if running && splits.len() < MAX_SPLITS => {
                        disable_raw_mode()?;
                        print!("\nEnter subgoal name: ");
                        io::stdout().flush()?;
                        let mut input = String::new();
                        io::stdin().read_line(&mut input)?;
                        enable_raw_mode()?;
                        let name = input.trim().to_string();
                        let parent = active;
                        let level = parent.map_or(0, |idx| splits[idx].level + 1);
                        let now = Instant::now();
                        let start_off = elapsed + now.duration_since(start_time);
                        let start_dt = Local::now();
                        splits.push(Split {
                            name,
                            start_offset: start_off,
                            end_offset: None,
                            start_dt,
                            end_dt: None,
                            parent,
                            level,
                        });
                        active = Some(splits.len() - 1);
                        draw_static(
                            &mut stdout,
                            &main_goal,
                            elapsed + now.duration_since(start_time),
                            &splits,
                        )?;
                    }
                    KeyCode::Char('n')
                        if running && active.is_some() && splits.len() < MAX_SPLITS =>
                    {
                        disable_raw_mode()?;
                        print!("\nEnter nested subgoal name: ");
                        io::stdout().flush()?;
                        let mut input = String::new();
                        io::stdin().read_line(&mut input)?;
                        enable_raw_mode()?;
                        let name = input.trim().to_string();
                        let parent = active.unwrap();
                        let level = splits[parent].level + 1;
                        let now = Instant::now();
                        let start_off = elapsed + now.duration_since(start_time);
                        let start_dt = Local::now();
                        splits.push(Split {
                            name,
                            start_offset: start_off,
                            end_offset: None,
                            start_dt,
                            end_dt: None,
                            parent: Some(parent),
                            level,
                        });
                        active = Some(splits.len() - 1);
                        draw_static(
                            &mut stdout,
                            &main_goal,
                            elapsed + now.duration_since(start_time),
                            &splits,
                        )?;
                    }
                    KeyCode::Char('h') => {
                        if let Some(idx) = active {
                            let now = Instant::now();
                            let end_off = elapsed + now.duration_since(start_time);
                            let end_dt = Local::now();
                            splits[idx].end_offset = Some(end_off);
                            splits[idx].end_dt = Some(end_dt);
                            active = splits[idx].parent;
                            draw_static(
                                &mut stdout,
                                &main_goal,
                                elapsed + now.duration_since(start_time),
                                &splits,
                            )?;
                        }
                    }
                    KeyCode::Char('u') => {
                        if let Some(idx) = active {
                            active = splits[idx].parent;
                            draw_static(&mut stdout, &main_goal, elapsed, &splits)?;
                        }
                    }
                    KeyCode::Char('d') => {
                        draw_static(&mut stdout, &main_goal, elapsed, &splits)?;
                    }
                    KeyCode::Char('t') if !running => {
                        if let Some(goal) = &main_goal {
                            let _ = save_log(goal, start_time, &splits, &log_file);
                        }
                    }
                    KeyCode::Char('q') => break,
                    _ => {}
                },
                Event::Resize(_, _) => {
                    // redraw on resize
                    let total = if running {
                        let now = Instant::now();
                        elapsed + now.duration_since(start_time)
                    } else {
                        elapsed
                    };
                    draw_static(&mut stdout, &main_goal, total, &splits)?;
                    if running {
                        let _ = draw_dynamic(&mut stdout, start_time, elapsed, &splits, &main_goal);
                    }
                }
                _ => {}
            },
            _ => {}
        }
    }

    disable_raw_mode()?;
    Ok(())
}
