import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest import mock

from curriculum import (
    CurriculumConfig,
    adaptive_training_window,
    find_champion_training_weights,
    run_iteration,
)


@contextmanager
def working_directory(path: Path):
    import os

    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


class CurriculumPolicyTest(unittest.TestCase):
    def test_training_window_expands_after_each_failed_promotion(self):
        champion = "exported_models/champion07.pt2"
        self.assertEqual(adaptive_training_window(8, champion, 4), 4)
        self.assertEqual(adaptive_training_window(9, champion, 4), 5)
        self.assertEqual(adaptive_training_window(10, champion, 4), 6)

    def test_bootstrap_uses_base_training_window(self):
        self.assertEqual(adaptive_training_window(0, "", 4), 4)

    def test_promoted_fp32_checkpoint_is_preferred(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            export_dir = Path(temp_dir)
            challenger = export_dir / "challenger03.pth"
            champion = export_dir / "champion03.pth"
            challenger.touch()
            champion.touch()

            selected = find_champion_training_weights(
                str(export_dir), str(export_dir / "champion03.pt2")
            )
            self.assertEqual(Path(selected), champion)

    def test_legacy_promoted_challenger_checkpoint_is_supported(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            export_dir = Path(temp_dir)
            challenger = export_dir / "challenger03.pth"
            challenger.touch()

            selected = find_champion_training_weights(
                str(export_dir), str(export_dir / "champion03.pt2")
            )
            self.assertEqual(Path(selected), challenger)

    def test_evaluation_game_count_must_balance_seats(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            schedule = Path(temp_dir) / "schedule.json"
            schedule.write_text('{"games_per_evaluation": 201}')
            with self.assertRaisesRegex(ValueError, "positive even"):
                CurriculumConfig.load(str(schedule))

    def test_iteration_uses_champion_and_adaptive_window(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            run_dir = Path(temp_dir)
            export_dir = run_dir / "exported_models"
            export_dir.mkdir()
            (run_dir / "data").mkdir()
            (run_dir / "generator").touch()
            (run_dir / "evaluator").touch()
            champion_pt = export_dir / "champion07.pt2"
            champion_pth = export_dir / "champion07.pth"
            previous_champion_pt = export_dir / "champion06.pt2"
            champion_pt.touch()
            champion_pth.touch()
            previous_champion_pt.touch()

            config = CurriculumConfig(
                data_dir="data",
                diagnostics_dir="diagnostics",
                model_export_path="exported_models",
                game_generator_bin="./generator",
                model_evaluator_bin="./evaluator",
                games_per_iteration=3,
                previous_champion_mix_fraction=0.2,
                simulations_per_move=800,
                games_per_evaluation=200,
                base_training_window_iterations=4,
            )
            commands = []
            train_arguments = {}

            def fake_subprocess(command, check):
                self.assertTrue(check)
                commands.append(command)
                if command[0] == "./evaluator":
                    evaluation_dir = Path(command[command.index("--out_dir") + 1])
                    evaluation_dir.mkdir(parents=True)
                    (evaluation_dir / "evaluation.json").write_text(
                        '{"challenger_win_rate": 0.50}'
                    )

            def fake_train(**kwargs):
                train_arguments.update(kwargs)
                Path(kwargs["save_path"]).touch()
                return 1.0, 0.5

            def fake_export(_weights_path, export_path):
                Path(export_path).touch()

            with working_directory(run_dir), mock.patch(
                "curriculum.subprocess.run", side_effect=fake_subprocess
            ), mock.patch("curriculum.train", side_effect=fake_train), mock.patch(
                "curriculum.export_to_aoti", side_effect=fake_export
            ):
                summary = run_iteration(10, config)

            self.assertFalse(summary.promoted)
            self.assertEqual(summary.training_window, 6)
            self.assertEqual(
                train_arguments["load_path"], "exported_models/champion07.pth"
            )
            self.assertEqual(train_arguments["iteration_window"], 6)
            self.assertIn("exported_models/champion07.pt2", commands[0])
            self.assertEqual(
                commands[0][
                    commands[0].index("--previous_champion_model_path") + 1
                ],
                "exported_models/champion06.pt2",
            )
            self.assertEqual(
                commands[0][
                    commands[0].index("--previous_champion_mix_fraction") + 1
                ],
                "0.2",
            )
            self.assertEqual(
                commands[1][commands[1].index("--games") + 1], "200"
            )
            self.assertEqual(
                commands[1][commands[1].index("--simulations") + 1], "800"
            )


if __name__ == "__main__":
    unittest.main()
