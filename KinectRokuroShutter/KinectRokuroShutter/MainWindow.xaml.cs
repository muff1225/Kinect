using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Microsoft.Kinect;
using Microsoft.Speech.Recognition;
using System.Threading;
using System.IO;
using Microsoft.Speech.AudioFormat;
using System.Diagnostics;
using System.Windows.Threading;

namespace KinectRokuroShutter
{
    public partial class MainWindow : Window
    {
        KinectSensor sensor;
        SpeechRecognitionEngine speechRecognizer;

        DispatcherTimer readyTimer;

        byte[] colorBytes;
        Skeleton[] skeletons;
        
        bool isCirclesVisible = true;

        bool isRokuroGestureActive = false;
        SolidColorBrush activeBrush = new SolidColorBrush(Colors.Green);
        SolidColorBrush inactiveBrush = new SolidColorBrush(Colors.Red);

        long picNum = 0;
        long picNumBefore = 0;

        public MainWindow()
        {
            InitializeComponent();

            //Runtime initialization is handled when the window is opened. When the window
            //is closed, the runtime MUST be unitialized.
            this.Loaded += new RoutedEventHandler(MainWindow_Loaded);
            //Handle the content obtained from the video camera, once received.

            this.KeyDown += new KeyEventHandler(MainWindow_KeyDown);
        }

        void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            sensor = KinectSensor.KinectSensors.FirstOrDefault();

            if (sensor == null)
            {
                MessageBox.Show("This application requires a Kinect sensor.");
                this.Close();
            }
            
            sensor.Start();

            sensor.ColorStream.Enable(ColorImageFormat.RgbResolution640x480Fps30);
            sensor.ColorFrameReady += new EventHandler<ColorImageFrameReadyEventArgs>(sensor_ColorFrameReady);

            sensor.DepthStream.Enable(DepthImageFormat.Resolution320x240Fps30);
            sensor.SkeletonStream.Enable();
            sensor.SkeletonFrameReady += new EventHandler<SkeletonFrameReadyEventArgs>(sensor_SkeletonFrameReady);

            sensor.ElevationAngle = 0;

            Application.Current.Exit += new ExitEventHandler(Current_Exit);

            InitializeSpeechRecognition();
        }

        void Current_Exit(object sender, ExitEventArgs e)
        {
            if (speechRecognizer != null)
            {
                speechRecognizer.RecognizeAsyncCancel();
                speechRecognizer.RecognizeAsyncStop();
            }
            if (sensor != null)
            {
                sensor.AudioSource.Stop();
                sensor.Stop();
                sensor.Dispose();
                sensor = null;
            }
        }

        void MainWindow_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.C)
            {
                ToggleCircles();
            }
        }

        void sensor_ColorFrameReady(object sender, ColorImageFrameReadyEventArgs e)
        {
            using (var image = e.OpenColorImageFrame())
            {
                if (image == null)
                    return;

                if (colorBytes == null ||
                    colorBytes.Length != image.PixelDataLength)
                {
                    colorBytes = new byte[image.PixelDataLength];
                }

                image.CopyPixelDataTo(colorBytes);

                //You could use PixelFormats.Bgr32 below to ignore the alpha,
                //or if you need to set the alpha you would loop through the bytes 
                //as in this loop below
                int length = colorBytes.Length;
                for (int i = 0; i < length; i += 4)
                {
                    colorBytes[i + 3] = 255;
                }

                BitmapSource source = BitmapSource.Create(image.Width,
                    image.Height,
                    96,
                    96,
                    PixelFormats.Bgra32,
                    null,
                    colorBytes,
                    image.Width * image.BytesPerPixel);
                videoImage.Source = source;
            }
        }


        void sensor_SkeletonFrameReady(object sender, SkeletonFrameReadyEventArgs e)
        {
            using (var skeletonFrame = e.OpenSkeletonFrame())
            {
                if (skeletonFrame == null)
                    return;

                if (skeletons == null ||
                    skeletons.Length != skeletonFrame.SkeletonArrayLength)
                {
                    skeletons = new Skeleton[skeletonFrame.SkeletonArrayLength];
                }

                skeletonFrame.CopySkeletonDataTo(skeletons);

                Skeleton closestSkeleton = (from s in skeletons
                                            where s.TrackingState == SkeletonTrackingState.Tracked &&
                                                  s.Joints[JointType.Head].TrackingState == JointTrackingState.Tracked
                                            select s).OrderBy(s => s.Joints[JointType.Head].Position.Z)
                                                    .FirstOrDefault();

                if (closestSkeleton == null)
                    return;

                var head = closestSkeleton.Joints[JointType.Head];
                var rightHand = closestSkeleton.Joints[JointType.HandRight];
                var leftHand = closestSkeleton.Joints[JointType.HandLeft];
                var rightElbow = closestSkeleton.Joints[JointType.ElbowRight];
                var leftElbow = closestSkeleton.Joints[JointType.ElbowLeft];

                if (head.TrackingState != JointTrackingState.Tracked ||
                    rightHand.TrackingState != JointTrackingState.Tracked ||
                    leftHand.TrackingState != JointTrackingState.Tracked ||
                    rightElbow.TrackingState != JointTrackingState.Tracked ||
                    leftElbow.TrackingState != JointTrackingState.Tracked)
                {
                    //Don't have a good read on the joints so we cannot process gestures
                    return;
                }

                SetEllipsePosition(ellipseHead, head, false);
                SetEllipsePosition(ellipseLeftHand, leftHand, isRokuroGestureActive);
                SetEllipsePosition(ellipseRightHand, rightHand, isRokuroGestureActive);
                SetEllipsePosition(ellipseLeftElbow, leftElbow, isRokuroGestureActive);
                SetEllipsePosition(ellipseRightElbow, rightElbow, isRokuroGestureActive);

                ProcessForwardBackGesture(head, rightHand, leftHand, rightElbow, leftElbow);
                SetRokuroCount();
            }
        }

        private void ProcessForwardBackGesture(Joint head, Joint rightHand, Joint leftHand, Joint rightElbow, Joint leftElbow)
        {
            if (rightHand.Position.X < head.Position.X + 0.16 &&
                leftHand.Position.X > head.Position.X - 0.16
                && (rightHand.Position.Y > head.Position.Y - 0.7
                && rightHand.Position.Y < head.Position.Y - 0.3)
                && (leftHand.Position.Y > head.Position.Y - 0.7 
                && leftHand.Position.Y < head.Position.Y - 0.3))
            {
                if ((rightElbow.Position.Y < rightHand.Position.Y + 0.2 && 
                    rightElbow.Position.Y > rightHand.Position.Y - 0.2)
                    && (leftElbow.Position.Y < leftHand.Position.Y + 0.2 &&
                    leftElbow.Position.Y > leftHand.Position.Y - 0.2))
                {
                    if (!isRokuroGestureActive)
                    {
                        isRokuroGestureActive = true;
                        GetRokuroCapture();
                 }
                }
            }
            else
            {
                isRokuroGestureActive = false;
            }
        }

        private void SetRokuroCount()
        {
            if (picNumBefore < picNum)
            {
                RokuroCount.Text = "適正度：" + picNum.ToString();
                picNumBefore = picNum;
            }
        }

        private void GetRokuroCapture()
        {
            picNum++;
            var bit = new RenderTargetBitmap(
            (int)videoImage.Width,
            (int)videoImage.Height,
            96, 96,
            PixelFormats.Pbgra32);
            bit.Render(videoImage);

            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(bit));
            string dir = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
            using (FileStream fs = File.Open(System.IO.Path.Combine(dir, "RockRowCap" + picNum + ".png"), FileMode.Create))
            {
                enc.Save(fs);
            }
        }

        //This method is used to position the ellipses on the canvas
        //according to correct movements of the tracked joints.
        private void SetEllipsePosition(Ellipse ellipse, Joint joint, bool isHighlighted)
        {
            var point = sensor.MapSkeletonPointToColor(joint.Position, sensor.ColorStream.Format);

            if (isHighlighted)
            {
                ellipse.Width = 30;
                ellipse.Height = 30;
                ellipse.Fill = activeBrush;
            }
            else
            {
                ellipse.Width = 20;
                ellipse.Height = 20;
                ellipse.Fill = inactiveBrush;
            }

            Canvas.SetLeft(ellipse, point.X - ellipse.ActualWidth / 2);
            Canvas.SetTop(ellipse, point.Y - ellipse.ActualHeight / 2);
        }

        void ToggleCircles()
        {
            if (isCirclesVisible)
                HideCircles();
            else
                ShowCircles();
        }

        void HideCircles()
        {
            isCirclesVisible = false;
            ellipseHead.Visibility = System.Windows.Visibility.Collapsed;
            ellipseLeftHand.Visibility = System.Windows.Visibility.Collapsed;
            ellipseRightHand.Visibility = System.Windows.Visibility.Collapsed;
            ellipseLeftElbow.Visibility = System.Windows.Visibility.Collapsed;
            ellipseRightElbow.Visibility = System.Windows.Visibility.Collapsed;
        }

        void ShowCircles()
        {
            isCirclesVisible = true;
            ellipseHead.Visibility = System.Windows.Visibility.Visible;
            ellipseLeftHand.Visibility = System.Windows.Visibility.Visible;
            ellipseRightHand.Visibility = System.Windows.Visibility.Visible;
            ellipseLeftElbow.Visibility = System.Windows.Visibility.Visible;
            ellipseRightElbow.Visibility = System.Windows.Visibility.Visible;
        }

        #region Speech Recognition Methods

        private static RecognizerInfo GetKinectRecognizer()
        {
            Func<RecognizerInfo, bool> matchingFunc = r =>
            {
                string value;
                r.AdditionalInfo.TryGetValue("Kinect", out value);
                return "True".Equals(value, StringComparison.InvariantCultureIgnoreCase) && "en-US".Equals(r.Culture.Name, StringComparison.InvariantCultureIgnoreCase);
            };
            return SpeechRecognitionEngine.InstalledRecognizers().Where(matchingFunc).FirstOrDefault();
        }

        private void InitializeSpeechRecognition()
        {
            RecognizerInfo ri = GetKinectRecognizer();
            if (ri == null)
            {
                MessageBox.Show(
                    @"There was a problem initializing Speech Recognition.
Ensure you have the Microsoft Speech SDK installed.",
                    "Failed to load Speech SDK",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                return;
            }

            try
            {
                speechRecognizer = new SpeechRecognitionEngine(ri.Id);
            }
            catch
            {
                MessageBox.Show(
                    @"There was a problem initializing Speech Recognition.
Ensure you have the Microsoft Speech SDK installed and configured.",
                    "Failed to load Speech SDK",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
            }

            var phrases = new Choices();
            phrases.Add("rock row");

            var gb = new GrammarBuilder();
            //Specify the culture to match the recognizer in case we are running in a different culture.                                 
            gb.Culture = ri.Culture;
            gb.Append(phrases);

            // Create the actual Grammar instance, and then load it into the speech recognizer.
            var g = new Grammar(gb);

            speechRecognizer.LoadGrammar(g);
            speechRecognizer.SpeechRecognized += SreSpeechRecognized;
            speechRecognizer.SpeechHypothesized += SreSpeechHypothesized;
            speechRecognizer.SpeechRecognitionRejected += SreSpeechRecognitionRejected;

            this.readyTimer = new DispatcherTimer();
            this.readyTimer.Tick += this.ReadyTimerTick;
            this.readyTimer.Interval = new TimeSpan(0, 0, 4);
            this.readyTimer.Start();

        }

        private void ReadyTimerTick(object sender, EventArgs e)
        {
            this.StartSpeechRecognition();
            this.readyTimer.Stop();
            this.readyTimer = null;
        }

        private void StartSpeechRecognition()
        {
            if (sensor == null || speechRecognizer == null)
                return;

            var audioSource = this.sensor.AudioSource;
            audioSource.BeamAngleMode = BeamAngleMode.Adaptive;
            var kinectStream = audioSource.Start();
                
            speechRecognizer.SetInputToAudioStream(
                    kinectStream, new SpeechAudioFormatInfo(EncodingFormat.Pcm, 16000, 16, 1, 32000, 2, null));
            speechRecognizer.RecognizeAsync(RecognizeMode.Multiple);
            
        }

        void SreSpeechRecognitionRejected(object sender, SpeechRecognitionRejectedEventArgs e)
        {
            Trace.WriteLine("\nSpeech Rejected, confidence: " + e.Result.Confidence);
        }

        void SreSpeechHypothesized(object sender, SpeechHypothesizedEventArgs e)
        {
            Trace.Write("\rSpeech Hypothesized: \t{0}", e.Result.Text);
        }

        void SreSpeechRecognized(object sender, SpeechRecognizedEventArgs e)
        {
            //This first release of the Kinect language pack doesn't have a reliable confidence model, so 
            //we don't use e.Result.Confidence here.
            if (e.Result.Confidence < 0.70)
            {
                Trace.WriteLine("\nSpeech Rejected filtered, confidence: " + e.Result.Confidence);
                return;
            }

            Trace.WriteLine("\nSpeech Recognized, confidence: " + e.Result.Confidence + ": \t{0}", e.Result.Text);
            if (e.Result.Text == "rock row")
            {
                this.Dispatcher.BeginInvoke((Action)delegate
                {
                    SetRokuroCount();
                    GetRokuroCapture();
                }); 
            }
        }
        
        #endregion

    }
}
